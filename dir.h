#pragma once

#include "inode.h"

ino_t dir_create(disk_t *disk, ino_t parent);
int dir_destroy(disk_t *disk, ino_t directory);
int dir_reparent(disk_t *disk, ino_t directory, ino_t new_parent);

off_t dir_enumerate(disk_t *disk, ino_t directory, off_t offset, ino_t *ino_out, char *name_out, size_t namesize);

ino_t dir_lookup(disk_t *disk, ino_t directory, const char *name, size_t namesize);
int dir_insert(disk_t *disk, ino_t directory, const char *name, size_t namesize, ino_t target);
ino_t dir_remove(disk_t *disk, ino_t directory, const char *name, size_t namesize);
