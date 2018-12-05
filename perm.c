#include "perm.h"

#include <errno.h>

// check if the given credentials have the rights to a given kind of editing on a given inode
int perm_check(disk_t *disk, ino_t inode, int perms, uid_t user, gid_t group) {
    inode_info_t info;
    if (inode_getinfo(disk, inode, &info) < 0) {
        return -ENOENT;
    }

    if (user == 0) {
        return 0;
    }

    if (perms == PERM_UTIME) {
        if (user == info.owner) {
            return 0;
        }
        perms = PERM_WRITE;
    }

    if (user == info.owner) {
        return (perms & (info.mode >> 6) & 7) == perms ? 0 : -EACCES;
    }
    if (group == info.group) {
        return (perms & (info.mode >> 3) & 7) == perms ? 0 : -EACCES;
    }
    return (perms & info.mode & 7) == perms ? 0 : -EACCES;
}

// do the chmod, checking ownership and for legal modes
int perm_chmod(disk_t *disk, ino_t inode, mode_t mode, uid_t user) {
    inode_info_t info;
    if (inode_getinfo(disk, inode, &info) < 0) {
        return -ENOENT;
    }

    if ((mode & 07777) != mode) {
        return -EINVAL;
    }

    if (user != 0 && user != info.owner) {
        return -EACCES;
    }

    return inode_chmod(disk, inode, mode | (info.mode & ~07777));
}

// do the chown, checking for root
int perm_chown(disk_t *disk, ino_t inode, uid_t user, uid_t newuser, uid_t newgroup) {
    if (user != 0) {
        return -EACCES;
    }

    return inode_chown(disk, inode, newuser, newgroup);
}
