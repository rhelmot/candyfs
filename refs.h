#pragma once

#include "inode.h"

int refs_open(disk_t *disk, ino_t inode);
int refs_close(disk_t *disk, ino_t inode);

nlink_t refs_link(disk_t *disk, ino_t inode);
nlink_t refs_unlink(disk_t *disk, ino_t inode);

ino_t refs_dir_lookup_open(disk_t *disk, ino_t directory, const char *name, size_t namesize);
