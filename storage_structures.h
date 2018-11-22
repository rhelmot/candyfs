#include <assert.h>
#include <limits.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "storage_emulator.h"

#define BLOCKSIZE 512
#define CANDYFS_MAGIC 0xCA4D11F5

#define INO_EOF ((ino_t)LONG_MIN)
#define BLOCKNO_EOF ((blockno_t)LONG_MIN)

typedef signed long blockno_t;
typedef char data_block_t[BLOCKSIZE];

blockno_t inumber_to_blocknumber(fakedisk_t *disk, ino_t inumber);
void inumber_set_blocknumber(fakedisk_t *disk, ino_t inumber, blockno_t blocknumber);
ino_t allocate_inumber(fakedisk_t *disk);
void free_inumber(fakedisk_t *disk, ino_t inumber);

blockno_t allocate_block(fakedisk_t *disk);
void free_block(fakedisk_t *disk, blockno_t blockno);
void mkfs_storage(fakedisk_t *disk, int ilist_size);
