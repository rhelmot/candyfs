#define FUSE_USE_VERSION 29

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "path.h"
#include "disk.h"
#include "inode.h"
#include "refs.h"
#include "symlink.h"
#include "file.h"
#include "perm.h"
#include "dir.h"

#define GETDISK() ((disk_t*)fuse_get_context()->private_data)
#define GETUSER() (fuse_get_context()->uid)
#define GETGROUP() (fuse_get_context()->gid)

#define S(x) { assert(x); return 0; }
#define F(x, y) { if (((long)(x)) < 0) { assert(y); return (x); } }

#define I(x) (refs_close(disk, (x)) == 0)
#define P(x) (path_close(disk, (x)) == 0)

static int candy_getattr(const char *path, struct stat *st) {
    disk_t *disk = GETDISK();
    ino_t inode = path_resolve(disk, path, false, GETUSER(), GETGROUP());
    F(inode, true);

    // no permissions required

    inode_info_t info;
    int res = inode_getinfo(disk, inode, &info);
    F(res, I(inode));

    st->st_ino = inode;
    st->st_mode = info.mode;
    st->st_nlink = info.nlinks;
    st->st_uid = info.owner;
    st->st_gid = info.group;
    st->st_rdev = -1;
    st->st_size = info.size;
    st->st_blocks = (info.size / BLOCKSIZE) + (info.size % BLOCKSIZE != 0);

    st->st_atim = info.last_access;
    st->st_mtim = info.last_change;
    st->st_ctim = info.last_statchange;

    S(I(inode));
}

static int candy_readlink(const char *path, char *buffer, size_t len) {
    disk_t *disk = GETDISK();
    ino_t inode = path_resolve(disk, path, false, GETUSER(), GETGROUP());
    F(inode, true);

    // no permissions required

    size_t reallen = symlink_read(disk, inode, buffer, len - 1);
    F((ssize_t)reallen, I(inode));

    buffer[reallen] = 0;
    S(I(inode));
}

static int candy_mkdir(const char *path, mode_t mode) {
    disk_t *disk = GETDISK();

    path_t handle = path_open(disk, path, false, GETUSER(), GETGROUP(), -1);
    F(handle, true);

    int ores = path_mkdir(disk, handle, mode, GETUSER(), GETGROUP());
    F(ores, P(handle));

    S(P(handle));
}

static int candy_unlink(const char *path) {
    disk_t *disk = GETDISK();
    path_t handle = path_open(disk, path, false, GETUSER(), GETGROUP(), -1);
    F(handle, true);

    int ores = path_unlink(disk, handle, GETUSER(), GETGROUP());
    F(ores, P(handle));

    S(P(handle));
}

static int candy_rmdir(const char *path) {
    disk_t *disk = GETDISK();
    path_t handle = path_open(disk, path, false, GETUSER(), GETGROUP(), -1);
    F(handle, true);

    int ores = path_rmdir(disk, handle, GETUSER(), GETGROUP());
    F(ores, P(handle));

    S(P(handle));
}

static int candy_symlink(const char *linkname, const char *path) {
    disk_t *disk = GETDISK();

    ino_t inode = symlink_create(disk, linkname);
    F(inode, true);
    assert(refs_open(disk, inode) == 0);

    assert(perm_chown(disk, inode, 0, GETUSER(), GETGROUP()) == 0);

    path_t handle = path_open(disk, path, false, GETUSER(), GETGROUP(), -1);
    F(handle, I(inode));

    int ores = path_link(disk, handle, inode, GETUSER(), GETGROUP());
    F(ores, I(inode) && P(handle));

    S(I(inode) && P(handle));
}

static int candy_rename(const char *oldpath, const char *newpath) {
    // TODO test if fuse will transform newpath by appending the basename
    // if it does not, perhaps add ideal_basename to open_path
    disk_t *disk = GETDISK();

    path_t dstpath = path_open(disk, newpath, false, GETUSER(), GETGROUP(), -1);
    F(dstpath, true);

    path_t srcpath = path_open(disk, oldpath, false, GETUSER(), GETGROUP(), dstpath);
    if (srcpath == -EWOULDBLOCK) {
        S(P(dstpath));
    }
    F(srcpath, P(dstpath));

    int ores = path_rename(disk, dstpath, srcpath, GETUSER(), GETGROUP());
    F(ores, P(dstpath) && P(srcpath));

    S(P(dstpath) && P(srcpath));
}

static int candy_link(const char *oldpath, const char *newpath) {
    disk_t *disk = GETDISK();

    ino_t inode = path_resolve(disk, oldpath, false, GETUSER(), GETGROUP());
    F(inode, true);

    path_t dstpath = path_open(disk, newpath, false, GETUSER(), GETGROUP(), -1);
    F(dstpath, I(inode));

    int ores = path_link(disk, dstpath, inode, GETUSER(), GETGROUP());
    F(ores, P(dstpath) && I(inode));

    S(P(dstpath) && I(inode));
}

static int candy_chmod(const char *path, mode_t mode) {
    disk_t *disk = GETDISK();

    ino_t inode = path_resolve(disk, path, true, GETUSER(), GETGROUP());
    F(inode, true);

    int ores = perm_chmod(disk, inode, mode & 07777, GETUSER());
    F(ores, I(inode));

    S(I(inode));
}

static int candy_chown(const char *path, uid_t user, gid_t group) {
    disk_t *disk = GETDISK();

    ino_t inode = path_resolve(disk, path, true, GETUSER(), GETGROUP());
    F(inode, true);

    int ores = perm_chown(disk, inode, GETUSER(), user, group);
    F(ores, I(inode));

    S(I(inode));
}

static int candy_truncate(const char *path, off_t size) {
    disk_t *disk = GETDISK();

    ino_t inode = path_resolve(disk, path, true, GETUSER(), GETGROUP());
    F(inode, true);

    off_t ores = file_truncate(disk, inode, size);
    F(ores, I(inode));
    if (ores != size) {
        F(-ENOSPC, I(inode));
    }

    S(I(inode));
}

static int candy_open(const char *path, struct fuse_file_info *fi) {
    disk_t *disk = GETDISK();

    ino_t inode = path_resolve(disk, path, true, GETUSER(), GETGROUP());
    F(inode, true);

    inode_info_t info;
    assert(inode_getinfo(disk, inode, &info) == 0);
    if (S_ISDIR(info.mode)) {
        F(-EISDIR, I(inode));
    }
    if (!S_ISREG(info.mode)) {
        F(-EINVAL, I(inode));
    }

    int access_mode;
    if ((fi->flags & O_ACCMODE) == O_RDONLY) {
        access_mode = PERM_READ;
    } else if ((fi->flags & O_ACCMODE) == O_WRONLY) {
        access_mode = PERM_WRITE;
    } else if ((fi->flags & O_ACCMODE) == O_RDWR) {
        access_mode = PERM_READ | PERM_WRITE;
    } else {
        F(-EINVAL, I(inode));
    }

    int ores = perm_check(disk, inode, access_mode, GETUSER(), GETGROUP());
    F(ores, I(inode));

    fi->fh = (uint64_t)inode;
    S(true);
}

static int candy_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    disk_t *disk = GETDISK();
    ino_t inode = (ino_t)fi->fh;

    return file_read(disk, inode, offset, buffer, size);
}


static int candy_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)path;
    disk_t *disk = GETDISK();
    ino_t inode = (ino_t)fi->fh;

    if ((fi->flags & O_APPEND) == O_APPEND) {
        offset = -1;
    }

    return file_write(disk, inode, offset, buf, size);
}

static int candy_statfs(const char *path, struct statvfs *fs) {
    (void)path;
    block_stat(GETDISK(), fs);
    fs->f_namemax = NAME_MAX;
    S(true);
}

// missing: flush

static int candy_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    disk_t *disk = GETDISK();
    S(I((ino_t)fi->fh));
}

// missing: fsync
// missing: xattr nonsense

static int candy_opendir(const char *path, struct fuse_file_info *fi) {
    disk_t *disk = GETDISK();

    ino_t inode = path_resolve(disk, path, true, GETUSER(), GETGROUP());
    F(inode, true);

    inode_info_t info;
    assert(inode_getinfo(disk, inode, &info) == 0);
    if (!S_ISDIR(info.mode)) {
        F(-ENOTDIR, I(inode));
    }

    int access_mode;
    if ((fi->flags & O_ACCMODE) == O_RDONLY) {
        access_mode = PERM_READ;
    } else if ((fi->flags & O_ACCMODE) == O_WRONLY) {
        access_mode = PERM_WRITE;
    } else if ((fi->flags & O_ACCMODE) == O_RDWR) {
        access_mode = PERM_READ | PERM_WRITE;
    } else {
        F(-EINVAL, I(inode));
    }

    int ores = perm_check(disk, inode, access_mode, GETUSER(), GETGROUP());
    F(ores, I(inode));

    fi->fh = (uint64_t)inode;
    S(true);
}

static int candy_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *fi) {
    (void)path;
    disk_t *disk = GETDISK();
    char name[NAME_MAX + 1];
    ino_t inode;

    do {
        off = dir_enumerate(disk, (ino_t)fi->fh, off, &inode, name, NAME_MAX + 1);
        F(off, true);

    } while (off != 0 && filler(buf, name, NULL, off) == 0);

    S(true);
}

static int candy_releasedir(const char *path, struct fuse_file_info *fi) {
    (void)path;
    disk_t *disk = GETDISK();
    S(I((ino_t)fi->fh));
}

// missing: fsyncdir
// missing: init?
// missing: destroy?

static int candy_access(const char *path, int flags) {
    disk_t *disk = GETDISK();

    ino_t inode = path_resolve(disk, path, true, GETUSER(), GETGROUP());
    F(inode, true);

    if (flags == F_OK) {
        S(I(inode));
    }

    int access_mode = 0;
    if (flags & R_OK) {
        access_mode |= PERM_READ;
    }
    if (flags & W_OK) {
        access_mode |= PERM_WRITE;
    }
    if (flags & X_OK) {
        access_mode |= PERM_EXEC;
    }

    int ores = perm_check(disk, inode, access_mode, GETUSER(), GETGROUP());
    F(ores, I(inode));

    S(I(inode));
}

static int candy_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    disk_t *disk = GETDISK();

    if (!S_ISREG(mode)) {
        F(-EINVAL, true);
    }

    ino_t inode = file_create(disk);
    F(inode, true);
    assert(refs_open(disk, inode) == 0);

    assert(perm_chown(disk, inode, 0, GETUSER(), GETGROUP()) == 0);
    assert(perm_chmod(disk, inode, mode & 07777, GETUSER()) == 0);

    path_t handle = path_open(disk, path, false, GETUSER(), GETGROUP(), -1);
    F(handle, I(inode));

    int ores = path_link(disk, handle, inode, GETUSER(), GETGROUP());
    F(ores, I(inode) && P(handle));

    fi->fh = (uint64_t)inode;
    S(P(handle));
}

static int candy_ftruncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void)path;
    disk_t *disk = GETDISK();
    ino_t inode = (ino_t)fi->fh;

    off_t ores = file_truncate(disk, inode, size);
    F(ores, true);
    if (ores != size) {
        F(-ENOSPC, true);
    }

    S(true);
}

static int candy_fgetattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    (void)path;
    disk_t *disk = GETDISK();
    ino_t inode = (ino_t)fi->fh;

    inode_info_t info;
    int res = inode_getinfo(disk, inode, &info);
    F(res, true);

    st->st_ino = inode;
    st->st_mode = info.mode;
    st->st_nlink = info.nlinks;
    st->st_uid = info.owner;
    st->st_gid = info.group;
    st->st_rdev = -1;
    st->st_size = info.size;
    st->st_blocks = (info.size / BLOCKSIZE) + (info.size % BLOCKSIZE != 0);

    st->st_atim = info.last_access;
    st->st_mtim = info.last_change;
    st->st_ctim = info.last_statchange;

    S(true);
}

// missing: lock

static int candy_utimens(const char *path, const struct timespec tv[2]) {
    disk_t *disk = GETDISK();
    ino_t inode = path_resolve(disk, path, false, GETUSER(), GETGROUP());
    F(inode, true);

    int ores = perm_check(disk, inode, PERM_UTIME, GETUSER(), GETGROUP());
    F(ores, I(inode));

    ores = inode_utime(disk, inode, &tv[0], &tv[1]);
    F(ores, I(inode));

    S(I(inode));
}

// missing: bmap
// "missing": ioctl
// missing: poll
// missing: write_buf
// missing: read_buf
// missing: flock
// missing: fallocate

static struct fuse_operations operations = {
    .getattr = candy_getattr,
    .readlink = candy_readlink,
    .mkdir = candy_mkdir,
    .unlink = candy_unlink,
    .rmdir = candy_rmdir,
    .symlink = candy_symlink,
    .rename = candy_rename,
    .link = candy_link,
    .chmod = candy_chmod,
    .chown = candy_chown,
    .truncate = candy_truncate,
    .open = candy_open,
    .read = candy_read,
    .write = candy_write,
    .statfs = candy_statfs,
    .release = candy_release,
    .opendir = candy_opendir,
    .readdir = candy_readdir,
    .releasedir = candy_releasedir,
    .access = candy_access,
    .create = candy_create,
    .ftruncate = candy_ftruncate,
    .fgetattr = candy_fgetattr,
    .utimens = candy_utimens,

    .flag_nullpath_ok = 1,
    .flag_nopath = 1,
};

void usage() {
    puts("Usage: mount.candyfs [device] mountpoint");
    exit(1);
}

int main(int argc, char *argv[]) {
    // desired options: -s -ohard_remove -ofsname=/dev/whatever -oblkdev -ouse_ino -oallow_other [mountpoint]

    if (argc == 2) {
        disk_t *disk = disk_create(1024*1024, BLOCKSIZE);
        mkfs_storage(disk, 1024);
        assert(mkfs_path(disk, getuid(), getgid()) == 0);

        char *args[] = {
            argv[0], "-d", "-s", "-ohard_remove", "-ouse_ino", "-oallow_other", argv[1], NULL
        };
        return fuse_main(7, args, &operations, disk);
    } else if (argc == 3) {
        disk_t *disk = disk_open(argv[1], BLOCKSIZE);
        if (!disk) {
            puts("Could not open device");
            exit(1);
        }

        char superblock[BLOCKSIZE];
        disk_read(disk, 0, superblock);
        if (*(unsigned int*)&superblock[0] != CANDYFS_MAGIC) {
            puts("Device is not candyfs formatted");
            exit(1);
        }

        char fsname[1024];
        snprintf(fsname, 1024, "-ofsname=%s", argv[1]);
        char *args[] = {
            argv[0], "-s", "-ohard_remove", fsname, "-oblkdev", "-ouse_ino", "-oallow_other", argv[2], NULL
        };
        return fuse_main(8, args, &operations, disk);
    } else {
        usage();
    }
}
