#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#include "disk.h"

disk_t *disk_create(unsigned long nblocks, int blocksize) {
	unsigned long fullsize = nblocks*blocksize;
	struct fakedisk *disk = malloc(sizeof(struct fakedisk) + fullsize);
	if (disk == NULL) {
		return NULL;
	}
	disk->nblocks = nblocks;
	disk->blocksize = blocksize;
	disk->fd = -1;
	return disk;
}

disk_t *disk_open(const char *path, int blocksize) {
	struct fakedisk *disk = malloc(sizeof(struct fakedisk));
	if (disk == NULL) {
		return NULL;
	}

	disk->fd = open(path, O_RDWR);
	if (disk->fd < 0) {
		free(disk);
		return NULL;
	}

	if (blocksize == -1) {
		if (ioctl(disk->fd, BLKBSZGET, &disk->blocksize) < 0) {
			free(disk);
			return NULL;
		}
	} else {
		disk->blocksize = blocksize;
	}

	if (ioctl(disk->fd, BLKGETSIZE64, &disk->nblocks) < 0) {
		free(disk);
		return NULL;
	}

	disk->nblocks /= disk->blocksize;
	return disk;
}

void disk_close(disk_t *disk) {
	if (disk->fd != -1) {
		close(disk->fd);
	}
	free(disk);
}

void disk_read(disk_t *disk, unsigned long blockno, void* block){
	if(blockno < 0 || blockno >= disk->nblocks){
		return;
	}

	if (disk->fd == -1) {
		memcpy(block, &disk->data[blockno*disk->blocksize], disk->blocksize);
	} else {
		if (lseek(disk->fd, blockno*disk->blocksize, SEEK_SET) != (off_t)(blockno*disk->blocksize)) {
			abort();
		}
		if (read(disk->fd, block, disk->blocksize) != disk->blocksize) {
			abort();
		}
	}
}

void disk_write(disk_t *disk, unsigned long blockno, void* block) {
	if (blockno < 0 || blockno >= disk->nblocks) {
		return;
	}

	if (disk->fd == -1) {
		memcpy(&disk->data[blockno * disk->blocksize], block, disk->blocksize);
	} else {
		if (lseek(disk->fd, blockno*disk->blocksize, SEEK_SET) != (off_t)(blockno*disk->blocksize)) {
			abort();
		}
		if (write(disk->fd, block, disk->blocksize) != disk->blocksize) {
			abort();
		}
	}
}
