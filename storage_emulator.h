typedef struct fakedisk {
    int nblocks;
    int blocksize;
    char data[0];
} fakedisk_t;

typedef char *block_t;

fakedisk_t *create_disk(int nblocks, int blocksize);

void destroy_disk(fakedisk_t *);

void read_block(fakedisk_t *disk, int blockno, block_t block);

void write_block(fakedisk_t *disk, int blockno, block_t block);
