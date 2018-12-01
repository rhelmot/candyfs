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

// internal: return a pointer in the hashmap to either the pointer to the relevant open_file_node OR the place where a new open_file_node for the given inode SHOULD be inserted if none currently exists
open_file_node_t **find_node_loc(ino_t inode) {
    size_t hash = inode % HASHMAP_SIZE;

    open_file_node_t **target = &open_file_table[hash];
    while (*target && (*target)->inode < inode) { // kept sorted for sanity's sake
	target = &(*target)->next;
    }
    return target;
}

// internal: return a pointer in the hashmap to the open_file_node for the given inode, or NULL if none exists
open_file_node_t *find_node(ino_t inode) {
    open_file_node_t **target = find_node_loc(inode);
    if (!*target || (*target)->inode != inode) {
	return NULL;
    }
    return *target;
}

// "open" an inode, holding a reference to it in a hash map or incrementing its reference count
int refs_open(disk_t *disk, ino_t inode) {
    open_file_node_t **target = find_node_loc(inode);

    if (*target && (*target)->inode == inode) {
	// file is already open. inc its refcount
	(*target)->refcount++;
    } else {
	// file is newly opened. make a node and insert it
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

// "close" an inode, decrementing its reference count. if both its refcount and its nlinks fall to zero, free it
int refs_close(disk_t *disk, ino_t inode) {
    open_file_node_t **target = find_node_loc(inode);

    if (!*target || (*target)->inode != inode) {
	return -1;
    }

    if (--(*target)->refcount <= 0) { // <= for safety, just in case we miss a code path for free
	// refcount has hit zero. free the open file table entry and check if we should free the inode too
	nlink_t nlinks = (*target)->nlinks;
	open_file_node_t *next = (*target)->next;
	free(*target);
	*target = next;

	if (nlinks == 0) { // == is okay - we only set nlinks in absolute terms
	    // GOOD BYE
	    if (inode_free(disk, inode) <= 0) {
		return -1;
	    }
	}
    }

    return 0;
}

// safely increase the number of links on an inode
nlink_t refs_link(disk_t *disk, ino_t inode) {
    open_file_node_t *node = find_node(inode);
    if (!node) {
	return -1;
    }
    node->nlinks = inode_link(disk, inode);
    return node->nlinks;
}

// safely decrease the muber of links on an inode
nlink_t refs_unlink(disk_t *disk, ino_t inode) {
    open_file_node_t *node = find_node(inode);
    if (!node) {
	return -1;
    }
    node->nlinks = inode_unlink(disk, inode);
    return node->nlinks;
}

// atomic version of dir_lookup + refs_open (only prevents races if there are no other calls to dir_lookup)
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
