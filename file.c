#include "file.h"

ino_t file_create(disk_t *disk) {
    ino_t file = inode_allocate(disk);
    if (file < 0) {
        return -1;
    }

    assert(inode_chmod(disk, symlink, S_IFREG | 0777) >= 0);
    return file;
}

ssize_t file_read(disk_t *disk, ino_t file, off_t pos, void *data, ssize_t size) {
    inode_info_t info;
    if (inode_getinfo(disk, file, &info) < 0) {
        return -1;
    }
    if (!S_ISREG(info.mode)) {
        return -1;
    }

    return inode_read(disk, file, pos, data, size);
}

ssize_t file_write(disk_t *disk, ino_t file, off_t pos, const void *data, ssize_t size) {
    inode_info_t info;
    if (inode_getinfo(disk, file, &info) < 0) {
        return -1;
    }
    if (!S_ISREG(info.mode)) {
        return -1;
    }

    return inode_write(disk, file, pos, data, size);
}

off_t file_truncate(disk_t *disk, ino_t file, off_t size) {
    inode_info_t info;
    if (inode_getinfo(disk, file, &info) < 0) {
        return -1;
    }
    if (!S_ISREG(info.mode)) {
        return -1;
    }

    return inode_truncate(disk, file,size);
}
