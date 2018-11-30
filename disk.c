#include <stdlib.h>
#include <string.h>

#include "disk.h"

disk_t *disk_create(int nblocks, int blocksize) {
    int fullsize = nblocks*blocksize;
    struct fakedisk *disk = malloc(sizeof(struct fakedisk) + fullsize);
    disk->nblocks = nblocks;
    disk->blocksize = blocksize;
    return disk;
}

void disk_read(disk_t *disk, int blockno, void* block){
    if(blockno < 0 || blockno >= disk->nblocks){
        return;
    }
    memcpy(block, &disk->data[blockno*disk->blocksize], disk->blocksize);
}

void disk_destroy(disk_t *disk) {
    free(disk);
}

void disk_write(disk_t *disk, int blockno, void* block) {
    if (blockno < 0 || blockno >= disk->nblocks) {
        return;
    }

    memcpy(&disk->data[blockno * disk->blocksize], block, disk->blocksize);
    /*write block  in the byteoffset determined by 
    the blocknumber intended to be written and the 
    size of the block inside the fake disk's data 
    section*/
}