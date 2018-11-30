#include "inodes.h"

int refs_open(fakedisk_t *disk, ino_t inode);
int refs_close(fakedisk_t *disk, ino_t inode);
nlink_t refs_link(fakedisk_t *disk, ino_t inode);
nlink_t refs_unlink(fakedisk_t *disk, ino_t inode);
