#include <stdlib.h>
#include <string.h>

#include "storage_emulator.h"

fakedisk_t *create_disk(int nblocks, int blocksize) {
    int fullsize = nblocks*blocksize;
    struct fakedisk *disk = malloc(sizeof(struct fakedisk) + fullsize);
    disk->nblocks = nblocks;
    disk->blocksize = blocksize;
    return disk;
}

void read_block(fakedisk_t *disk, int blockno, void* block){
    if(blockno < 0 || blockno >= disk->nblocks){
        return;
    }
    memcpy(block, &disk->data[blockno*disk->blocksize], disk->blocksize);
}

void destroy_disk(fakedisk_t *disk) {
    free(disk);
}

void write_block(fakedisk_t *disk, int blockno, void* block) {
    if (blockno < 0 || blockno >= disk->nblocks) {
        return;
    }

    memcpy(&disk->data[blockno * disk->blocksize], block, disk->blocksize);
}
