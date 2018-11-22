typedef struct fakedisk {
    unsigned int nblocks;
    unsigned int blocksize;
    char data[0];
} fakedisk_t;

fakedisk_t *create_disk(int nblocks, int blocksize);

void destroy_disk(fakedisk_t *disk);

void read_block(fakedisk_t *disk, int blockno, void* block);

void write_block(fakedisk_t *disk, int blockno, void* block);
