#include "dir.h"

#include <string.h>

// Walk the directory tree to convert a path to an inumber
// see path_resolution(7)
// I _believe_ fuse should never make us deref a symlink here?
// if I'm wrong about that, just add a check next to the isdir and add a recursion counter
// curdir should be the inumber of the current working directory
// rootdir should be the inumber of the root directory (0, except for cases of chroot. does fuse handle that for us?)
ino_t namei(disk_t *disk, const char* path, ino_t curdir, ino_t rootdir) {
    const char *token = path;
    ino_t current = curdir;

    if (*token == '/') {
	current = rootdir;
	token++;
    }

    for (const char *endtoken = strchr(token, '/'); endtoken != NULL; token = endtoken+1, endtoken = strchr(token, '/')) {
	if (endtoken == token) {
	    continue;
	}

	size_t tokensize = endtoken - token;
	current = dir_lookup(disk, current, token, tokensize);
	if (current < 0) {
	    return current;
	}

	inode_info_t info;
	inode_get_info(disk, current, &info);
	if (!S_ISDIR(info.mode)) {
	    return -1;
	}
    }

    if (*token) {
	current = dir_lookup(disk, current, token, strlen(token));
    }

    return current;
}

int mkfs_tree(disk_t *disk) {
    assert(dir_create(disk, 0, 0, 0) == 0);
    return 0;
}
