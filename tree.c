#include "tree.h"
#include "dir.h"
#include "perm.h"
#include "symlink.h"
#include "refs.h"

#include <string.h>

ino_t namei_rec(disk_t *disk, const char* path, ino_t *curdir, ino_t rootdir, bool deref, uid_t user, gid_t group, int level);

// do a single traversal from curdir to current, checking permissions and dereferencing links if that's what we want.
// fills the inode_info_t struct for the target.
// will take ownership of one new inode
ino_t namei_internal_single(disk_t *disk, const char *token, size_t tokensize, ino_t *curdir, ino_t rootdir, uid_t user, gid_t group, bool deref, inode_info_t *info, int *level) {
    // check if we have search permission on this directory
    int res = perm_check(disk, *curdir, PERM_EXEC, user, group);
    if (res < 0) {
	return res;
    }
    if (!res) {
	return -1;
    }

    // actually do the lookup!
    ino_t current = refs_dir_lookup_open(disk, *curdir, token, tokensize);
    if (current < 0) {
	return current;
    }

    // check the type of the result
    if (inode_getinfo(disk, current, info) < 0) {
	// this should never happen. should it be an assertion?
	return -1;
    }

    // derfef symlinks if we need
    if (deref && S_ISLNK(info->mode)) {
	char linkpath[PATH_MAX];
	if (symlink_read(disk, current, linkpath, PATH_MAX) < 0) {
	    return -1;
	}

	assert(refs_close(disk, current) == 0);
	(*level)++;
	current = namei_rec(disk, linkpath, curdir, rootdir, true, user, group, *level);
	// reload the inode info
	if (inode_getinfo(disk, current, info) < 0) {
	    return -1;
	}
    }
    return current;
}


ino_t namei_rec(disk_t *disk, const char* path, ino_t *curdir, ino_t rootdir, bool deref, uid_t user, gid_t group, int level) {
    if (level > 8) {
	return -1;
    }

    inode_info_t info;
    const char *token = path;
    ino_t current = *curdir;
    if (refs_open(disk, current) != 0) {
	return -1;
    }
    // TODO make sure the curdir semantics work in the following cases:
    // - ordinary
    // - nonexistent file
    // - nonexistent file ending in /
    // - link pointing at nonexistent file

    // part 1 - choose root
    if (*token == '/') {
	assert(refs_close(disk, current) == 0);
	assert(refs_close(disk, *curdir) == 0);

	current = rootdir;
	*curdir = rootdir;
	token++;

	assert(refs_open(disk, current) == 0);
	assert(refs_open(disk, *curdir) == 0);
    }

    // part 2 - traverse directories
    for (const char *endtoken = strchr(token, '/'); endtoken != NULL; token = endtoken+1, endtoken = strchr(token, '/')) {
	if (endtoken == token) {
	    continue;
	}

	size_t tokensize = endtoken - token;
	assert(refs_close(disk, *curdir) == 0);
	*curdir = current; // this is the only assignment to curdir
	current = namei_internal_single(disk, token, tokensize, curdir, rootdir, user, group, true, &info, &level);
	if (current < 0) {
	    assert(refs_close(disk, *curdir) == 0);
	    *curdir = INO_EOF;
	    return current;
	}
	if (!S_ISDIR(info.mode)) {
	    assert(refs_close(disk, *curdir) == 0);
	    *curdir = INO_EOF;
	    return -1;
	}
    }

    // part 3. if there's a remaining path component look it up!
    if (*token) {
	assert(refs_close(disk, *curdir) == 0);
	*curdir = current; // this is the only other assignment to curdir (just a mirror don't worry)
	current = namei_internal_single(disk, token, strlen(token), curdir, rootdir, user, group, deref, &info, &level);
	if (current < 0) {
	    // special return case
	    return INO_EOF;
	}
    }

    return current;
}

// translate a path name into an inode. returns the resulting inode or an error code or INO_EOF. If an inode is returned, it will already have ownership.
// see path_resolution(7)
//
// path: the path to translate, null-terminated
// curdir: a pointer to the current directory's inode. you should have ownership over this value. It will be released and replaced with an owned copy of the inode which is the last directory in the chain before the final lookup. Useful for mkdir and create and such. This outparam is only valid if the return value is >= 0 OR it's INO_EOF. INO_EOF indicates that the traversal succeeded until the final lookup, i.e. suitable for create or mkdir. If you pass INO_EOF in this field it will cause this to always start from the root. If you pass a null pointer, it will have the same effect.
// rootdir: the inode for the root directory. Should have ownership. Or, can be INO_EOF in which the root will be 0 and you don't need to handle ownership.
// deref: whether to allow returning inodes for symlinks or return the inode for the dereferenced target.
// user: the current uid for permissions checking
// group: the current gid for permissions checking
//
ino_t namei(disk_t *disk, const char* path, ino_t *curdir, ino_t rootdir, bool deref, uid_t user, gid_t group) {
    bool usemyroot = rootdir == INO_EOF;
    bool usemycurdir = curdir == NULL;
    ino_t mycurdir = INO_EOF;

    if (usemyroot) {
	rootdir = 0;
	assert(refs_open(disk, rootdir) == 0);
    }
    if (usemycurdir) {
	curdir = &mycurdir;
    }

    if (*curdir == INO_EOF) {
	*curdir = rootdir;
	assert(refs_open(disk, *curdir) == 0);
    }

    ino_t result = namei_rec(disk, path, curdir, rootdir, deref, user, group, 0);

    if (usemyroot) {
	assert(refs_close(disk, rootdir) == 0);
    }
    if (usemycurdir) {
	assert(refs_close(disk, *curdir) == 0);
    }

    return result;
}

int mkfs_tree(disk_t *disk) {
    assert(dir_create(disk, 0) == 0);
    assert(perm_chmod(disk, 0, 0755, 0) == 0);
    return 0;
}
