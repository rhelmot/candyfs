#include <stdlib.h>

#include "storage_emulator.h"

fakedisk_t *create_disk(int nblocks, int blocksize) {
    int fullsize = nblocks*blocksize;
    struct fakedisk *disk = malloc(sizeof(struct fakedisk) + fullsize);
    disk->nblocks = nblocks;
    disk->blocksize = blocksize;
    return disk;
}
