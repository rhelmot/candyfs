typedef struct fakedisk fakedisk_t;
typedef char *block_t;

fakedisk_t *create_disk(int nblocks, int blocksize);

void destroy_disk(fakedisk_t *);

block_t read_block(fakedisk_t *disk, int blockno);

void write_block(fakedisk_t *disk, int blockno, block_t block);
