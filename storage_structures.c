#include "storage_emulator.h"

#define BLOCKSIZE 512

typedef struct superblock {
    int magic;
    int ilist_size;
} superblock_t;

typedef int ino_t;
typedef int blockno_t;

#define INUMS_PER_ILIST_BLOCK (BLOCKSIZE / sizeof(blockno_t))
typedef blockno_t ilist_block_t[INUMS_PER_ILIST_BLOCK];

int x() {
    ilist_block_t myblock;
    assert(sizeof(myblock) == BLOCKSIZE);

    blockno_t my_block_num = myblock[0];
    myblock[INUMS_PER_ILIST_BLOCK - 1];

}

int inumber_to_blocknumber(fakedisk_t *disk, int inumber) {
    ilist_block_t myblock;
    read_block(disk, 1 + inumber / INUMS_PER_ILIST_BLOCK, (block_t)myblock);
    return myblock[inumber % INUMS_PER_ILIST_BLOCK];
}
