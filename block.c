#include "block.h"


/*SUPERBLOCK fields
*size of filesystem
*
*number of freeblocks int he file system
*list of free blocks available
*index of next free block in the list of free blocks
*
*size of inode list
*
*number of free inodes in the FS
*a list of free inodes in the FS
*index of the next free inode in the list of free inodes
*
*lock fields for the 
*flag to indicate the SUPERBLOCK has been modified
*/
#define SUPERBLOCK_HEAD \
    int magic;                  \
    int ilist_size;             \
    blockno_t freelist_start;   \
    ino_t ino_freelist_start;

typedef struct superblock {
    SUPERBLOCK_HEAD
    char _pad[BLOCKSIZE - sizeof(struct { SUPERBLOCK_HEAD })];
} superblock_t;

#define BLOCKNUMS_PER_FREELIST_BLOCK ((int)(BLOCKSIZE / sizeof(blockno_t) - 1))
typedef struct freelist_block {
    blockno_t next;
    blockno_t blocks[BLOCKNUMS_PER_FREELIST_BLOCK];
} freelist_block_t;

#define INUMS_PER_ILIST_BLOCK ((int)(BLOCKSIZE / sizeof(blockno_t)))
typedef blockno_t ilist_block_t[INUMS_PER_ILIST_BLOCK];

_Static_assert(sizeof(superblock_t) == BLOCKSIZE, "superblock is not blocksize");
_Static_assert(sizeof(freelist_block_t) == BLOCKSIZE, "freelist block is not blocksize");
_Static_assert(sizeof(ilist_block_t) == BLOCKSIZE, "ilist block is not blocksize");
_Static_assert(sizeof(data_block_t) == BLOCKSIZE, "data block is not blocksize");

blockno_t ino_get(disk_t *disk, ino_t inumber) {
    ilist_block_t myblock;
    disk_read(disk, 1 + inumber / INUMS_PER_ILIST_BLOCK, myblock);
    return myblock[inumber % INUMS_PER_ILIST_BLOCK];
}

void ino_set(disk_t *disk, ino_t inumber, blockno_t blocknumber) {
    ilist_block_t myblock;
    disk_read(disk, 1 + inumber / INUMS_PER_ILIST_BLOCK, myblock);
    myblock[inumber % INUMS_PER_ILIST_BLOCK] = blocknumber;
    disk_write(disk, 1 + inumber / INUMS_PER_ILIST_BLOCK, myblock);
}

ino_t ino_allocate(disk_t *disk) {
    superblock_t superblock;
    disk_read(disk, 0, &superblock);
    ino_t result = superblock.ino_freelist_start;
    if (result != INO_EOF) {
        superblock.ino_freelist_start = -ino_get(disk, result);
        disk_write(disk, 0, &superblock);
    }
    return result;
}

void ino_free(disk_t *disk, ino_t inumber) {
    superblock_t superblock;
    disk_read(disk, 0, &superblock);
    ino_set(disk, inumber, -superblock.ino_freelist_start);
    superblock.ino_freelist_start = inumber;
    disk_write(disk, 0, &superblock);
}

blockno_t block_allocate(disk_t *disk) {
    superblock_t superblock;
    disk_read(disk, 0, &superblock);
    if (superblock.freelist_start == BLOCKNO_EOF) {
        return BLOCKNO_EOF;
    }

    freelist_block_t freelist_block;
    disk_read(disk, superblock.freelist_start, &freelist_block);
    for (int i = 0; i < BLOCKNUMS_PER_FREELIST_BLOCK; i++) {
        blockno_t candidate = freelist_block.blocks[i];
        if (candidate != BLOCKNO_EOF) {
            freelist_block.blocks[i] = BLOCKNO_EOF;
            disk_write(disk, superblock.freelist_start, &freelist_block);
            return candidate;
        }
    }

    blockno_t vagabond = superblock.freelist_start;
    superblock.freelist_start = freelist_block.next;
    disk_write(disk, 0, &superblock);
    return vagabond;
}

void block_free(disk_t *disk, blockno_t blockno) {
    superblock_t superblock;
    disk_read(disk, 0, &superblock);

    if (superblock.freelist_start != BLOCKNO_EOF) {
        freelist_block_t freelist_head;
        disk_read(disk, superblock.freelist_start, &freelist_head);
        for (int i = BLOCKNUMS_PER_FREELIST_BLOCK - 1; i >= 0; i--) {
            if (freelist_head.blocks[i] == BLOCKNO_EOF) {
                freelist_head.blocks[i] = blockno;
                disk_write(disk, superblock.freelist_start, &freelist_head);
                return;
            }
        }
    }

    freelist_block_t vagabond_block;
    vagabond_block.next = superblock.freelist_start;
    for (int i = 0; i < BLOCKNUMS_PER_FREELIST_BLOCK; i++) {
        vagabond_block.blocks[i] = BLOCKNO_EOF;
    }
    superblock.freelist_start = blockno;
    disk_write(disk, 0, &superblock);
    disk_write(disk, blockno, &vagabond_block);
}

// ilist size is number of ilist blocks
void mkfs_storage(disk_t *disk, int ilist_size) {
    int num_data_blocks = disk->nblocks - ilist_size - 1;
    blockno_t first_data_block = ilist_size + 1;
    assert(num_data_blocks > 0);

    superblock_t superblock;
    superblock.magic = CANDYFS_MAGIC;
    superblock.ilist_size = ilist_size;
    superblock.freelist_start = first_data_block;
    superblock.ino_freelist_start = 0;
    disk_write(disk, 0, &superblock);

    for (int i = 0; i < ilist_size; i++) {
        ilist_block_t iblock;
        for (int j = 0; j < INUMS_PER_ILIST_BLOCK; j++) {
            iblock[j] = -(j + INUMS_PER_ILIST_BLOCK*i + 1);
        }
        if (i == ilist_size - 1) {
            iblock[INUMS_PER_ILIST_BLOCK - 1] = BLOCKNO_EOF;
        }
        disk_write(disk, i + 1, iblock);
    }

    for (blockno_t i = first_data_block; i < disk->nblocks; i += BLOCKNUMS_PER_FREELIST_BLOCK + 1) {
        freelist_block_t freelist_entry;
        freelist_entry.next = i + BLOCKNUMS_PER_FREELIST_BLOCK + 1;
        if (freelist_entry.next >= disk->nblocks) {
            freelist_entry.next = BLOCKNO_EOF;
        }
        for (int j = 0; j < BLOCKNUMS_PER_FREELIST_BLOCK; j++) {
            blockno_t target_block = i + 1 + j;
            if (target_block >= disk->nblocks) {
                target_block = BLOCKNO_EOF;
            }
            freelist_entry.blocks[j] = target_block;
        }

        disk_write(disk, i, &freelist_entry);
    }
}

