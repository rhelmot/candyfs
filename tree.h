#pragma once

#include "inode.h"

#include <stdbool.h>

ino_t namei(disk_t *disk, const char* path, ino_t *curdir, ino_t rootdir, bool deref, uid_t user, gid_t group);
int mkfs_tree(disk_t *disk);
