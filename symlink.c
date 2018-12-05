#include "symlink.h"

#include <string.h>
#include <errno.h>

// create a symlink, returning the new inode
ino_t symlink_create(disk_t *disk, const char* filename) {
    size_t namesize = strlen(filename);
    if (namesize == 0 || namesize > PATH_MAX - 1) {
        return -ENAMETOOLONG;
    }

    ino_t symlink = inode_allocate(disk);
    if ((long)symlink < 0) {
        return -ENOSPC;
    }

    assert(inode_chmod(disk, symlink, S_IFLNK | 0777) == 0);

    if (inode_write(disk, symlink, 0, filename, namesize) < (ssize_t)namesize) {
        assert(inode_free(disk, symlink) == 0);
        return -ENOSPC;
    }

    return symlink;
}

// basically readlink(2)
ino_t symlink_read(disk_t *disk, ino_t symlink, char *filename, size_t maxsize) {
    inode_info_t info;
    if (inode_getinfo(disk, symlink, &info) < 0) {
        return -ENOENT;
    }
    if (!S_ISLNK(info.mode)) {
        return -EINVAL;
    }

    return inode_read(disk, symlink, 0, filename, maxsize);
}
