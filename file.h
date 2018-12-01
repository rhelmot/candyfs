#pragma once

#include "inode.h"

ino_t file_create(disk_t *disk);
ssize_t file_read(disk_t *disk, ino_t file, off_t pos, void *data, ssize_t size);
ssize_t file_write(disk_t *disk, ino_t file, off_t pos, const void *data, ssize_t size);
off_t file_truncate(disk_t *disk, ino_t file, off_t size);
