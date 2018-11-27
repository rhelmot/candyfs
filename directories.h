#include "inodes.h"

ino_t dir_allocate(fakedisk_t *disk, ino_t parent, uid_t owner, gid_t group);
int dir_reparent(fakedisk_t *disk, ino_t directory, ino_t new_parent);
int dir_free(fakedisk_t *disk, ino_t directory);

ino_t dir_lookup(fakedisk_t *disk, ino_t directory, const char *name, size_t namesize);
int dir_insert(fakedisk_t *disk, ino_t directory, const char *name, size_t namesize, ino_t target);
ino_t dir_remove(fakedisk_t *disk, ino_t directory, const char *name, size_t namesize);
