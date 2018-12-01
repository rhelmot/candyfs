#pragma once

#include "inode.h"

#define PERM_READ 4
#define PERM_WRITE 2
#define PERM_EXEC 1

int perm_check(disk_t *disk, ino_t inode, int perms, uid_t user, gid_t group);
int perm_chmod(disk_t *disk, ino_t inode, mode_t mode, uid_t user);
int perm_chown(disk_t *disk, ino_t inode, uid_t user, uid_t newuser, uid_t newgroup);
