#include "perm.h"

// check if the given credentials have the rights to a given kind of editing on a given inode
int perm_check(disk_t *disk, ino_t inode, int perms, uid_t user, gid_t group) {
    inode_info_t info;
    if (inode_getinfo(disk, inode, &info) < 0) {
        return -1;
    }

    if (user == 0) {
        return 1;
    }

    if (user == info.owner) {
        return (perms & (info.mode >> 6) & 7) == perms;
    }
    if (group == info.group) {
        return (perms & (info.mode >> 3) & 7) == perms;
    }
    return (perms & info.mode & 7) == perms;
}

// do the chmod, checking ownership and for legal modes
int perm_chmod(disk_t *disk, ino_t inode, mode_t mode, uid_t user) {
    inode_info_t info;
    if (inode_getinfo(disk, inode, &info) < 0) {
        return -1;
    }

    if ((mode & 07777) != mode) {
        return -1;
    }

    if (user != 0 && user != info.owner) {
        return -1;
    }

    return inode_chmod(disk, inode, mode);
}

// do the chown, checking for root
int perm_chown(disk_t *disk, ino_t inode, uid_t user, uid_t newuser, uid_t newgroup) {
    if (user != 0) {
        return -1;
    }

    return inode_chown(disk, inode, newuser, newgroup);
}
