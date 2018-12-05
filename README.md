🍬 CandyFS 🍬

By Audrey Dutcher and absolutely nobody else

To build: install a C compiler supporting the gnu C11 standard and the fuse development package.
run make.
It will produce two files.

- `mkfs.candyfs`: the mkfs program - taking a disk and formatting it.
  It can take a `--user` argument indicating to make the root directory owned by the current user.
- `mount.candyfs`: the mount program - taking a disk and a mountpoint and putting them together.
  If you specify only a mountpoint, a filesystem of 4G will be created in RAM and formatted before mouting.

### Codebase

there are several modules, roughly in order of abstraction:

- disk
- block
- inode
- file
- dir
- symlink
- perm
- refs
- path

Each one has functions of the form `module_function`, for example `disk_open`.
All the exported functions are named in the corresponding header files.
Unfortunately, documentation is in the code files, not the header files.
Sometimes, it's nowhere at all!

The main programs are candyfs.c and mkfs.c.
There is also a test.c (tests the inode module), but maybe don't run it unless you set the blocksize down to 512 - it will try to fill up the disk as a stress test.

My development notes are in the notes file. Peruse at your leisure.
