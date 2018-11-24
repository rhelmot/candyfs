#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "inodes.h"

int main() {
    fakedisk_t *disk = create_disk(1024 * 1024, BLOCKSIZE);
    mkfs_storage(disk, 50);

    ino_t inum = inode_allocate(disk);

    char buf[BLOCKSIZE];
    char buf2[BLOCKSIZE];
    assert(inode_read(disk, inum, 0, buf, 1) == 0);
    assert(inode_read(disk, inum, 100, buf, 1) == 0);

    buf[0] = 'A';
    assert(inode_write(disk, inum, 0, buf, 1) == 1);
    buf[0] = 0;
    assert(inode_read(disk, inum, 0, buf, 1) == 1);
    assert(buf[0] == 'A');

    buf[0] = 'B';
    assert(inode_write(disk, inum, 100, buf, 1) == 1);
    buf[0] = 1;
    buf[1] = 0;
    assert(inode_read(disk, inum, 99, buf, 3) == 2);
    assert(buf[0] == 0);
    assert(buf[1] == 'B');

    assert(inode_truncate(disk, inum, 0) == 0);
    assert(inode_read(disk, inum, 0, buf, 1) == 0);

    off_t max_size = inode_truncate(disk, inum, 999999999999);
    char *huge = (char*)calloc(max_size, 1);
    char *huge2 = (char*)calloc(max_size, 1);
    printf("max size %lx\n", max_size);
    assert(max_size > 10000);
    inode_truncate(disk, inum, 0);

    for (off_t i = BLOCKSIZE; i < max_size; i += BLOCKSIZE) {
	memset(buf, (char)(i / BLOCKSIZE), BLOCKSIZE/2);
	assert(inode_write(disk, inum, i - BLOCKSIZE/4, buf, BLOCKSIZE/2) == BLOCKSIZE/2);
    }

    memset(buf2, 0, BLOCKSIZE);
    for (off_t i = BLOCKSIZE; i < max_size; i += BLOCKSIZE) {
	memset(buf, 0, BLOCKSIZE);
	memset(buf2 + (BLOCKSIZE / 4), (char)(i / BLOCKSIZE), BLOCKSIZE/2);
	memcpy(huge2 + i - BLOCKSIZE/2, buf2, BLOCKSIZE);

	assert(inode_read(disk, inum, i - BLOCKSIZE/2, buf, BLOCKSIZE) == (i == max_size - BLOCKSIZE ? BLOCKSIZE*3/4 : BLOCKSIZE));
	assert(!memcmp(buf, buf2, BLOCKSIZE));
    }

    off_t used_size = max_size - BLOCKSIZE*3/4;
    assert(inode_read(disk, inum, 0, huge, used_size) == used_size);
    assert(!memcmp(huge, huge2, used_size));
}
