#pragma once

typedef struct fakedisk {
    unsigned long nblocks;
    unsigned int blocksize;
    int fd;
    char data[0];
} disk_t;

disk_t *disk_create(unsigned long nblocks, int blocksize);
disk_t *disk_open(const char *path, int blocksize);
void disk_close(disk_t *disk);

void disk_read(disk_t *disk, unsigned long blockno, void* block);
void disk_write(disk_t *disk, unsigned long blockno, void* block);
