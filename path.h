#pragma once

#include "inode.h"

#include <stdbool.h>

typedef ssize_t path_t;

path_t path_open(disk_t *disk, const char *path, bool deref, uid_t user, gid_t group, path_t noblock);
int path_close(disk_t *disk, path_t path);
ino_t path_get(disk_t *disk, path_t path);
ino_t path_resolve(disk_t *disk, const char *path, bool deref, uid_t user, gid_t group);

int path_link(disk_t *disk, path_t path, ino_t inode, uid_t user, gid_t group);
int path_unlink(disk_t *disk, path_t path, uid_t user, gid_t group);

int path_mkdir(disk_t *disk, path_t path, mode_t mode, uid_t user, gid_t group);
int path_rmdir(disk_t *disk, path_t path, uid_t user, gid_t group);

ino_t path_rename(disk_t *disk, path_t dstpath, path_t srcpath, uid_t user, gid_t group);

int mkfs_path(disk_t *disk, uid_t owner, gid_t group);
