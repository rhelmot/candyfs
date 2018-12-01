#pragma once

typedef struct fakedisk {
    unsigned int nblocks;
    unsigned int blocksize;
    char data[0];
} disk_t;

disk_t *disk_create(int nblocks, int blocksize);

void disk_destroy(disk_t *disk);

void disk_read(disk_t *disk, int blockno, void* block);

void disk_write(disk_t *disk, int blockno, void* block);
