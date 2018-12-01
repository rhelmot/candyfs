#include "refs.h"
#include "dir.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct open_file_node {
    struct open_file_node *next;
    ino_t inode;
    unsigned int refcount;
    nlink_t nlinks;
} open_file_node_t;

#define HASHMAP_SIZE 53

open_file_node_t *open_file_table[HASHMAP_SIZE];

open_file_node_t **find_node_loc(ino_t inode) {
    size_t hash = inode % HASHMAP_SIZE;

    open_file_node_t **target = &open_file_table[hash];
    while (*target && (*target)->inode < inode) {
	target = &(*target)->next;
    }
    return target;
}

open_file_node_t *find_node(ino_t inode) {
    open_file_node_t **target = find_node_loc(inode);
    if (!*target || (*target)->inode != inode) {
	return NULL;
    }
    return *target;
}

int refs_open(disk_t *disk, ino_t inode) {
    open_file_node_t **target = find_node_loc(inode);

    if (*target && (*target)->inode == inode) {
	(*target)->refcount++;
    } else {
	inode_info_t info;
	if (inode_getinfo(disk, inode, &info) < 0) {
	    return -1;
	}

	open_file_node_t *newnode = (open_file_node_t*)malloc(sizeof(open_file_node_t));
	newnode->next = (*target)->next;
	newnode->inode = inode;
	newnode->refcount = 1;
	newnode->nlinks = info.nlinks;
	*target = newnode;
    }
    return 0;
}

int refs_close(disk_t *disk, ino_t inode) {
    open_file_node_t **target = find_node_loc(inode);

    if (!*target || (*target)->inode != inode) {
	return -1;
    }

    if (--(*target)->refcount <= 0) { // <= for safety, just in case we miss a code path for free
	nlink_t nlinks = (*target)->nlinks;
	open_file_node_t *next = (*target)->next;
	free(*target);
	*target = next;

	if (nlinks == 0) { // == is okay - we only set nlinks in absolute terms
	    if (inode_free(disk, inode) <= 0) {
		return -1;
	    }
	}
    }

    return 0;
}

nlink_t refs_link(disk_t *disk, ino_t inode) {
    open_file_node_t *node = find_node(inode);
    if (!node) {
	return -1;
    }
    node->nlinks = inode_link(disk, inode);
    return node->nlinks;
}

nlink_t refs_unlink(disk_t *disk, ino_t inode) {
    open_file_node_t *node = find_node(inode);
    if (!node) {
	return -1;
    }
    node->nlinks = inode_unlink(disk, inode);
    return node->nlinks;
}

ino_t refs_dir_lookup_open(disk_t *disk, ino_t directory, const char *name, size_t namesize) {
    ino_t out = dir_lookup(disk, directory, name, namesize);
    if (out < 0) {
	return out;
    }

    if (refs_open(disk, out) < 0) {
	return -1;
    }
    return out;
}
