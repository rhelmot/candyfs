#pragma once

#include "inode.h"

ino_t symlink_create(disk_t *disk, const char* filename);
ino_t symlink_read(disk_t *disk, ino_t symlink, char *filename, size_t maxsize);
