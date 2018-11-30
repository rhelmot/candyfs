#include "block.h"

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

	//mode is permissions and file type!

typedef struct inode_info {
	INODE_META
} inode_info_t;


ino_t inode_allocate(disk_t *disk);
int inode_free(disk_t *disk, ino_t inumber);

int inode_set_info(disk_t *disk, ino_t inumber, mode_t mode, uid_t owner, gid_t group);
int inode_get_info(disk_t *disk, ino_t inumber, inode_info_t *info);

ssize_t inode_write(disk_t *disk, ino_t inumber, off_t pos, void *data, ssize_t size);
ssize_t inode_read(disk_t *disk, ino_t inumber, off_t pos, void *data, ssize_t size);
off_t inode_truncate(disk_t *disk, ino_t inumber, off_t size);

nlink_t inode_link(disk_t *disk, ino_t inumber);
nlink_t inode_unlink(disk_t *disk, ino_t inumber);
