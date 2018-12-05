#include "path.h"
#include "dir.h"
#include "perm.h"
#include "symlink.h"
#include "refs.h"

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h> // temporary measure

#define DIE(x) { fprintf(stderr, x); abort(); }

// forward declaration
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

	// actually do the lookup!
	ino_t current = refs_dir_lookup_open(disk, *curdir, token, tokensize);
	if ((long)current < 0) {
		return current;
	}

	// check the type of the result
	if (inode_getinfo(disk, current, info) < 0) {
		// this should never happen. should it be an assertion?
		return -ENOENT;
	}

	// derfef symlinks if we need
	if (deref && S_ISLNK(info->mode)) {
		char linkpath[PATH_MAX];
		memset(linkpath, 0, PATH_MAX);
		assert(symlink_read(disk, current, linkpath, PATH_MAX) >= 0);

		assert(refs_close(disk, current) == 0);
		(*level)++;
		current = namei_rec(disk, linkpath, curdir, rootdir, true, user, group, *level);
		// reload the inode info
		if (inode_getinfo(disk, current, info) < 0) {
			return -ENOENT;
		}
	}
	return current;
}


ino_t namei_rec(disk_t *disk, const char* path, ino_t *curdir, ino_t rootdir, bool deref, uid_t user, gid_t group, int level) {
	if (level > 8) {
		return -ELOOP;
	}

	inode_info_t info;
	const char *token = path;
	ino_t current = *curdir;
	if (refs_open(disk, current) != 0) {
		return -ENOENT;
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
		if ((long)current < 0) {
			assert(refs_close(disk, *curdir) == 0);
			*curdir = INO_EOF;
			return current;
		}
		if (!S_ISDIR(info.mode)) {
			assert(refs_close(disk, *curdir) == 0);
			*curdir = INO_EOF;
			return -ENOTDIR;
		}
	}

	// part 3. if there's a remaining path component look it up!
	if (*token) {
		assert(refs_close(disk, *curdir) == 0);
		*curdir = current; // this is the only other assignment to curdir (just a mirror don't worry)
		current = namei_internal_single(disk, token, strlen(token), curdir, rootdir, user, group, deref, &info, &level);
		if ((long)current < 0) {
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

typedef struct open_path_node {
	// semaphore?
	int refs;
	ino_t parent_dir;
	char name[NAME_MAX];
	unsigned char namelen;
} open_path_node_t;

#define MAX_OPEN_PATHS 1024
open_path_node_t open_path_table[MAX_OPEN_PATHS];


// open a handle to a path. this can be a nonexistent filename in an existing directory
// noblock controls blocking. -1 = block. -2 = do not block. pass the value of an existing path_t to only block if the matching path is a duplicate of the given handle
path_t path_open(disk_t *disk, const char *path, bool deref, uid_t user, gid_t group, path_t noblock) {
	ino_t curdir = INO_EOF, rootdir = INO_EOF;

	ino_t target = namei(disk, path, &curdir, rootdir, deref, user, group);
	if ((long)target < 0 && target != INO_EOF) {
		return target;
	}
	if (target != INO_EOF) {
		assert(refs_close(disk, target) == 0);
	}

	size_t tokensize = strlen(path);
	const char *endtoken = path + tokensize;
	const char *token;

	// invariant: endtoken points to a / or a NUL
	while (endtoken > path && endtoken[-1] == '/') {
		endtoken--;
	}

	// endtoken either is at the beginning of the path or the character before it is NOT /
	if (endtoken == path) {
		token = ".";
		tokensize = 1;
	} else {
		// invariant: token points to a character which is not /
		token = endtoken - 1;

		while (token > path && token[-1] != '/') {
			token--;
		}

		tokensize = endtoken - token;
	}

	if (tokensize > NAME_MAX) {
		assert(refs_close(disk, curdir) == 0);
		return -ENAMETOOLONG;
	}

	// at this point we have a handle to the directory and also the name. let's go to town

	path_t handle;
	path_t chosen = -1;
	for (handle = 0; handle < MAX_OPEN_PATHS; handle++) {
		if (open_path_table[handle].refs == 0) {
			chosen = handle;
		} else {
			if (open_path_table[handle].parent_dir == curdir && 
					open_path_table[handle].namelen == tokensize &&
					memcmp(token, open_path_table[handle].name, tokensize) == 0) {
				// found an open reference to the path!
				if (noblock == handle || noblock == -2) {
					assert(refs_close(disk, curdir) == 0);
					return -EWOULDBLOCK;
				}
				// temporary measure
				DIE("CRITICAL ERROR: path-open blocking in single-threaded program");
			}
		}
	}

	if (chosen == -1) {
		assert(refs_close(disk, curdir) == 0);
		return -ENOMEM;
	}

	open_path_table[chosen].parent_dir = curdir;
	memcpy(open_path_table[chosen].name, token, tokensize);
	open_path_table[chosen].namelen = tokensize;
	open_path_table[chosen].refs = 1;
	return chosen;
}

int path_close(disk_t *disk, path_t path) {
	if (path >= MAX_OPEN_PATHS || path < 0 || open_path_table[path].refs == 0) {
		return -1; // user error
	}

	if (--open_path_table[path].refs == 0) {
		assert(refs_close(disk, open_path_table[path].parent_dir) == 0);
	} else {
		DIE("reached multiple references to a path in single-threaded program");
	}
	return 0;
}

// get the inode at the path. will always succeed for a valid path handle, returning either INO_EOF or an inode
// returned handle will have ownership
ino_t path_get(disk_t *disk, path_t path) {
	if (path >= MAX_OPEN_PATHS || path < 0 || open_path_table[path].refs == 0) {
		return -1; // user error
	}

	return refs_dir_lookup_open(disk, open_path_table[path].parent_dir, open_path_table[path].name, open_path_table[path].namelen);
}

// shortcut version of path_open -> path_get -> path_close
ino_t path_resolve(disk_t *disk, const char *path, bool deref, uid_t user, gid_t group) {
	ino_t res = namei(disk, path, NULL, INO_EOF, deref, user, group);
	if (res == INO_EOF) {
		res = -ENOENT;
	}
	return res;
}

// set the inode into the path. will fail if the target exists or if the src is a directory
int path_link(disk_t *disk, path_t path, ino_t inode, uid_t user, gid_t group) {
	inode_info_t info;
	int ores;

	// must provide valid path handle
	if (path >= MAX_OPEN_PATHS || path < 0 || open_path_table[path].refs == 0) {
		return -1; // user error
	}

	// src must exist and not be a directory
	ores = inode_getinfo(disk, inode, &info);
	if (ores < 0) {
		return ores;
	}
	if (S_ISDIR(info.mode)) {
		return -EPERM;
	}

	// require write access to dest directory
	ores = perm_check(disk, open_path_table[path].parent_dir, PERM_WRITE, user, group);
	if (ores < 0) {
		return ores;
	}

	// insert! may fail if dst exists already or if directory has been removed
	ores = dir_insert(disk, open_path_table[path].parent_dir, open_path_table[path].name, open_path_table[path].namelen, inode);
	if (ores < 0) {
		return ores;
	}
	assert(refs_link(disk, inode) == 0);
	return 0;
}

// remove the inode at the given path. will fail if it's a directory.
int path_unlink(disk_t *disk, path_t path, uid_t user, gid_t group) {
	inode_info_t info;
	int ores;

	// must provide valid path handle
	if (path >= MAX_OPEN_PATHS || path < 0 || open_path_table[path].refs == 0) {
		return -1; // user error
	}

	// file must exist and not be a directory
	ino_t inode = path_get(disk, path);
	if ((long)inode < 0) {
		return -ENOENT;
	}
	assert(inode_getinfo(disk, inode, &info) == 0);
	if (S_ISDIR(info.mode)) {
		return -EPERM;
	}

	// require write access to dest directory
	ores = perm_check(disk, open_path_table[path].parent_dir, PERM_WRITE, user, group);
	if (ores < 0) {
		assert(refs_close(disk, inode) == 0);
		return ores;
	}

	// remove. should not fail at this point...
	assert(dir_remove(disk, open_path_table[path].parent_dir, open_path_table[path].name, open_path_table[path].namelen) == inode);

	assert(refs_unlink(disk, inode) == 0);
	assert(refs_close(disk, inode) == 0);
	return 0;
}

// create a directory at the given path
int path_mkdir(disk_t *disk, path_t path, mode_t mode, uid_t user, gid_t group) {
	int ores;

	// must provide valid path handle
	if (path >= MAX_OPEN_PATHS || path < 0 || open_path_table[path].refs == 0) {
		return -1; // user error
	}

	// require write access to dest directory
	ores = perm_check(disk, open_path_table[path].parent_dir, PERM_WRITE, user, group);
	if (ores < 0) {
		return ores;
	}

	// create the directory
	ino_t directory = dir_create(disk, open_path_table[path].parent_dir);
	if ((long)directory < 0) {
		return directory;
	}

	// until we link it in, there's no need to call unlink if we want to destroy it. it already has nlinks 0
	// so refs_close will free it
	assert(refs_open(disk, directory) == 0);

	// set permissions
	ores = perm_chown(disk, directory, 0, user, group);
	if (ores < 0) {
		assert(refs_close(disk, directory) == 0);
		return ores;
	}
	ores = perm_chmod(disk, directory, mode, user);
	if (ores < 0) {
		assert(refs_close(disk, directory) == 0);
		return ores;
	}

	// insert! may fail if dst exists already or if parent directory has been removed
	ores = dir_insert(disk, open_path_table[path].parent_dir, open_path_table[path].name, open_path_table[path].namelen, directory);
	if (ores < 0) {
		assert(refs_close(disk, directory) == 0);
		return ores;
	}

	assert(refs_link(disk, directory) == 0);
	assert(refs_close(disk, directory) == 0);
	return 0;
}

// remove a directory at the given path. will fail if it's not empty
int path_rmdir(disk_t *disk, path_t path, uid_t user, gid_t group) {
	int ores;

	// must provide valid path handle
	if (path >= MAX_OPEN_PATHS || path < 0 || open_path_table[path].refs == 0) {
		return -1; // user error
	}

	// file must exist (dir_destroy will check it's actually a directory later)
	ino_t inode = path_get(disk, path);
	if ((long)inode < 0) {
		return -ENOENT;
	}

	// require write access to parent directory
	ores = perm_check(disk, open_path_table[path].parent_dir, PERM_WRITE, user, group);
	if (ores < 0) {
		assert(refs_close(disk, inode) == 0);
		return ores;
	}

	// cripple the directory, will fail if it's not empty or if it's not a directory at all!
	ores = dir_destroy(disk, inode);
	if (ores < 0) {
		assert(refs_close(disk, inode) == 0);
		return ores;
	}

	// remove. should not fail at this point...
	assert(dir_remove(disk, open_path_table[path].parent_dir, open_path_table[path].name, open_path_table[path].namelen) == inode);

	assert(refs_unlink(disk, inode) == 0);
	assert(refs_close(disk, inode) == 0);
	return 0;
}

// rename the element at the source path to the element at the dest path, unlinking the dest element if it exists
// will fail if src and dest are a file and directory or vice versa
ino_t path_rename(disk_t *disk, path_t dstpath, path_t srcpath, uid_t user, gid_t group) {
	inode_info_t info;
	bool isdir;
	int ores;

	// must provide valid path handles
	if (srcpath >= MAX_OPEN_PATHS || srcpath < 0 || open_path_table[srcpath].refs == 0) {
		return -1; // user error
	}
	if (dstpath >= MAX_OPEN_PATHS || dstpath < 0 || open_path_table[dstpath].refs == 0) {
		return -1; // user error
	}

	// get src inode - must exist
	ino_t inode = path_get(disk, srcpath);
	if ((long)inode < 0) {
		return -ENOENT;
	}

	// require write access to dest directory
	ores = perm_check(disk, open_path_table[dstpath].parent_dir, PERM_WRITE, user, group);
	if (ores < 0) {
		assert(refs_close(disk, inode) == 0);
		return ores;
	}
	// require write access to src directory
	ores = perm_check(disk, open_path_table[srcpath].parent_dir, PERM_WRITE, user, group);
	if (ores < 0) {
		assert(refs_close(disk, inode) == 0);
		return ores;
	}

	// figure out if src is a directory
	assert(inode_getinfo(disk, inode, &info) == 0);
	isdir = S_ISDIR(info.mode);

	// unlink the target if it exists
	ino_t current = path_get(disk, dstpath);
	if ((long)current >= 0) {
		assert(inode_getinfo(disk, current, &info) == 0);
		// special case: target is a directory
		if (S_ISDIR(info.mode)) {
			// ... so src must be a directory too
			if (!isdir) {
				assert(refs_close(disk, inode) == 0);
				assert(refs_close(disk, current) == 0);
				return -EISDIR;
			}

			// destroy the directory, aborting if it's not empty
			ores = dir_destroy(disk, current);
			if (ores < 0) {
				assert(refs_close(disk, inode) == 0);
				assert(refs_close(disk, current) == 0);
				return ores;
			}
		} else {
			// ... likewise if target is NOT a directory src must not be a directory as well
			if (isdir) {
				assert(refs_close(disk, inode) == 0);
				assert(refs_close(disk, current) == 0);
				return -ENOTDIR;
			}
		}

		// remove and unlink the target
		assert(dir_remove(disk, open_path_table[dstpath].parent_dir, open_path_table[dstpath].name, open_path_table[dstpath].namelen) == current);
		assert(refs_unlink(disk, current) == 0);
		assert(refs_close(disk, current) == 0);
	}

	// insert!
	// may fail because of an orphaned directory or such
	ores = dir_insert(disk, open_path_table[dstpath].parent_dir, open_path_table[dstpath].name, open_path_table[dstpath].namelen, inode);
	if (ores < 0) {
		assert(refs_close(disk, inode) == 0);
		return ores;
	}

	// remove!
	assert(dir_remove(disk, open_path_table[srcpath].parent_dir, open_path_table[srcpath].name, open_path_table[srcpath].namelen) == inode);

	// if needed, reparent!
	if (isdir) {
		assert(dir_reparent(disk, inode, open_path_table[dstpath].parent_dir) == 0);
	}

	// phew!
	assert(refs_close(disk, inode) == 0);
	return 0;
}

int mkfs_path(disk_t *disk, uid_t owner, gid_t group) {
	ino_t root = dir_create(disk, 0);
	assert(root == 0);
	assert(refs_open(disk, root) == 0);
	assert(refs_link(disk, root) == 0);
	assert(perm_chown(disk, root, 0, owner, group) == 0);
	assert(perm_chmod(disk, root, 0755, owner) == 0);
	assert(refs_close(disk, root) == 0);
	return 0;
}
