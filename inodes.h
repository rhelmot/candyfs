#include "storage_structures.h"

#define INODE_META \
	mode_t mode;                        \
	uid_t owner;                        \
	gid_t group;                        \
	nlink_t nlinks;                     \
	off_t size;                         \
	struct timespec created;            \
	struct timespec last_access;        \
	struct timespec last_change;        \
	struct timespec last_statchange;

typedef struct inode_info {
	INODE_META
} inode_info_t;

ino_t inode_allocate(fakedisk_t *disk);
int inode_free(fakedisk_t *disk, ino_t inumber);

int inode_set_info(fakedisk_t *disk, ino_t inumber, mode_t mode, uid_t owner, gid_t group, nlink_t nlinks);
int inode_get_info(fakedisk_t *disk, ino_t inumber, inode_info_t *info);

ssize_t inode_write(fakedisk_t *disk, ino_t inumber, off_t pos, void *data, ssize_t size);
ssize_t inode_read(fakedisk_t *disk, ino_t inumber, off_t pos, void *data, ssize_t size);
off_t inode_truncate(fakedisk_t *disk, ino_t inumber, off_t size);
