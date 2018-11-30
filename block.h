#include <assert.h>
#include <limits.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "disk.h"

#define BLOCKSIZE 512
#define CANDYFS_MAGIC 0xCA4D11F5

#define INO_EOF ((ino_t)LONG_MIN)
#define BLOCKNO_EOF ((blockno_t)LONG_MIN)

typedef signed long blockno_t;
typedef char data_block_t[BLOCKSIZE];

blockno_t ino_get(disk_t *disk, ino_t inumber);
void ino_set(disk_t *disk, ino_t inumber, blockno_t blocknumber);
ino_t ino_allocate(disk_t *disk);
void ino_free(disk_t *disk, ino_t inumber);

blockno_t block_allocate(disk_t *disk);
void block_free(disk_t *disk, blockno_t blockno);
void mkfs_storage(disk_t *disk, int ilist_size);
