#include "inode.h"

ino_t dir_create(disk_t *disk, ino_t parent, uid_t owner, gid_t group);
int dir_reparent(disk_t *disk, ino_t directory, ino_t new_parent);
int dir_free(disk_t *disk, ino_t directory);

ino_t dir_lookup(disk_t *disk, ino_t directory, const char *name, size_t namesize);
int dir_insert(disk_t *disk, ino_t directory, const char *name, size_t namesize, ino_t target);
ino_t dir_remove(disk_t *disk, ino_t directory, const char *name, size_t namesize);
