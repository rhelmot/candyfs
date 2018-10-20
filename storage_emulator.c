#include <stdlib.h>

#include "storage_emulator.h"

fakedisk_t *create_disk(int nblocks, int blocksize) {
    int fullsize = nblocks*blocksize;
    struct fakedisk *disk = malloc(sizeof(struct fakedisk) + fullsize);
    disk->nblocks = nblocks;
    disk->blocksize = blocksize;
    return disk;
}


void read_block(fakedisk_t *disk, int blockno, block_t block){
    if(blockno < 0 || blockno >= disk->nblocks){
        return;
    }
    memcpy(block, &disk->data[blockno*disk->blocksize], disk->blocksize);
}
