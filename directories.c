#include "inodes.h"

#include <string.h>

/* WAYYYY too complicated for now. tone it the fuck down, buster.
   typedef struct dir_map_block {
   struct {
      unsigned int hash : 24;
      unsigned int length : 8;
      unsigned int offset : 32;
      ino_t inumber : 64;
   } map[BLOCKSIZE / 16 - 1];
   unsigned int next_offset;
   unsigned int pad[3];
} dir_map_block_t;
*/

#define ENTRIES_PER_DIR_BLOCK (BLOCKSIZE / 4 / sizeof(ino_t))
#define NAMESPACE_PER_DIR_BLOCK (BLOCKSIZE / 4 * 3)

typedef struct dir_map_block {
	ino_t numbers[ENTRIES_PER_DIR_BLOCK];
	char names[NAMESPACE_PER_DIR_BLOCK];
} dir_map_block_t;

_Static_assert(sizeof(dir_map_block_t) == BLOCKSIZE, "dir_map block is not blocksize");
_Static_assert(ENTRIES_PER_DIR_BLOCK >= 2, "not enough dir entries per dir block");
_Static_assert(NAMESPACE_PER_DIR_BLOCK > 255, "not enough name space per block");

// allocate a new directory, return its inumber
ino_t dir_allocate(fakedisk_t *disk, ino_t parent, uid_t owner, gid_t group) {
	ino_t directory = inode_allocate(disk);
	if (directory < 0) {
		return -1;
	}

	// TODO: umask
	assert(inode_set_info(disk, directory, S_IFDIR | 0755, owner, group, 1) >= 0);

	dir_map_block_t block;
	block.numbers[0] = parent;
	block.numbers[1] = directory;
	strcpy(&block.names[0], "..");
	strcpy(&block.names[3], ".");

	for (unsigned int i = 2; i < ENTRIES_PER_DIR_BLOCK; i++) {
		block.numbers[i] = INO_EOF;
	}
	memset(&block.names[5], 0, NAMESPACE_PER_DIR_BLOCK - 5);

	if (inode_write(disk, directory, 0, &block, sizeof(block)) != sizeof(block)) {
		assert(inode_free(disk, directory) >= 0);
		return -1;
	}

	return directory;
}

// free the directory, failing if it contains anything
int dir_free(fakedisk_t *disk, ino_t directory) {
	inode_info_t info;
	dir_map_block_t block;
	if (inode_get_info(disk, directory, &info) < 0) {
		return -1;
	}
	if (!S_ISDIR(info.mode)) {
		return -1;
	}
	// this requires that our compaction works correctly, which it should, but how to test?
	if (info.size > (off_t)sizeof(block)) {
		return -1;
	}

	assert(inode_read(disk, directory, 0, &block, sizeof(block)) == sizeof(block));

	// see above
	if (block.numbers[2] != INO_EOF) {
		return -1;
	}

	return 0;
}

// change the parent inode entry
int dir_reparent(fakedisk_t *disk, ino_t directory, ino_t new_parent) {
	inode_info_t info;
	dir_map_block_t block;
	if (inode_get_info(disk, directory, &info) < 0) {
		return -1;
	}
	if (!S_ISDIR(info.mode)) {
		return -1;
	}

	assert(inode_read(disk, directory, 0, &block, sizeof(block)) == sizeof(block));
	assert(block.numbers[1] == directory);
	block.numbers[0] = new_parent;
	assert(inode_write(disk, directory, 0, &block, sizeof(block)) == sizeof(block));
	return 0;
}

// look up a dir entry, returning the target inode
ino_t dir_lookup(fakedisk_t *disk, ino_t directory, const char *name, size_t namesize) {
	inode_info_t info;
	dir_map_block_t block;
	if (inode_get_info(disk, directory, &info) < 0) {
		return -1;
	}
	if (!S_ISDIR(info.mode)) {
		return -1;
	}

	off_t pos = 0;
	if (namesize > 255) {
		return -1;
	}

	while (inode_read(disk, directory, pos, &block, sizeof(block)) == sizeof(block)) {
		int nameoff = 0;
		for (unsigned int i = 0; i < ENTRIES_PER_DIR_BLOCK && block.numbers[i] != INO_EOF; i++) {
			size_t curlen = strlen(&block.names[nameoff]);
			if (curlen == namesize && memcmp(&block.names[nameoff], name, namesize) == 0) {
				return block.numbers[i];
			}
			nameoff += curlen + 1;
		}
		pos += sizeof(block);
	}

	return -1;
}

// add a directory entry
int dir_insert(fakedisk_t *disk, ino_t directory, const char *name, size_t namesize, ino_t target) {
	inode_info_t info;
	dir_map_block_t block;
	dir_map_block_t bestblock;
	off_t pos = 0;
	off_t bestpos = -1;
	size_t bestnameoff = 0;
	unsigned int besti = 0;

	if (inode_get_info(disk, directory, &info) < 0) {
		return -1;
	}

	if (!S_ISDIR(info.mode)) {
		return -1;
	}

	if (namesize > 255) {
		return -1;
	}

	while (inode_read(disk, directory, pos, &block, sizeof(block)) == sizeof(block)) {
		size_t nameoff = 0;
		unsigned int i;
		for (i = 0; i < ENTRIES_PER_DIR_BLOCK && block.numbers[i] != INO_EOF; i++) {
			size_t curlen = strlen(&block.names[nameoff]);
			if (curlen == namesize && memcmp(&block.names[nameoff], name, namesize) == 0) {
				return -1;
			}
			nameoff += curlen + 1;
		}

		if (i < ENTRIES_PER_DIR_BLOCK && NAMESPACE_PER_DIR_BLOCK - nameoff > namesize && nameoff > bestnameoff) {
			bestblock = block;
			bestpos = pos;
			bestnameoff = nameoff;
			besti = i;
		}
		pos += sizeof(block);
	}

	if (bestpos == -1) {
		bestpos = pos;
		bestnameoff = 0;
		besti = 0;
		for (unsigned int i = 0; i < ENTRIES_PER_DIR_BLOCK; i++) {
			bestblock.numbers[i] = INO_EOF;
		}
		memset(bestblock.names, 0, NAMESPACE_PER_DIR_BLOCK);
	}

	memcpy(&bestblock.names[bestnameoff], name, namesize);
	bestblock.numbers[besti] = target;

	if (inode_write(disk, directory, bestpos, &bestblock, sizeof(bestblock)) != sizeof(bestblock)) {
		return -1;
	}

	return 0;
}

// remove a directory entry
ino_t dir_remove(fakedisk_t *disk, ino_t directory, const char *name, size_t namesize) {
	inode_info_t info;
	dir_map_block_t block;
	off_t pos = 0;
	int empty_count = 0;

	if (inode_get_info(disk, directory, &info) < 0) {
		return -1;
	}

	if (!S_ISDIR(info.mode)) {
		return -1;
	}

	if (namesize > 255) {
		return -1;
	}
	// disallow removing the self/parent entries
	if (namesize == 1 && name[0] == '.') {
		return -1;
	}
	if (namesize == 2 && name[0] == '.' && name[1] == '.') {
		return -1;
	}

	while (inode_read(disk, directory, pos, &block, sizeof(block)) == sizeof(block)) {
		size_t nameoff = 0;
		for (unsigned int i = 0; i < ENTRIES_PER_DIR_BLOCK && block.numbers[i] != INO_EOF; i++) {
			size_t curlen = strlen(&block.names[nameoff]);
			if (curlen == namesize && memcmp(&block.names[nameoff], name, namesize) == 0) {
				ino_t res = block.numbers[i];
				// found the thing we want to remove. two options:
				// 1) there's nothing else in this block, and it's the last block:
				//    do not write it back, instead truncate the file. perhaps multiple blocks.
				// 2) there are other things in the block, or it's not the last block:
				//    have to reorganize the contents of this block and write it back.
				if (i == 0 && block.numbers[1] == INO_EOF && pos + (off_t)sizeof(block) == info.size) {
					assert(inode_truncate(disk, directory, pos - empty_count * sizeof(block)) >= 0);
				} else {
					memmove(&block.numbers[i], &block.numbers[i+1], (ENTRIES_PER_DIR_BLOCK - (i+1)) * sizeof(ino_t));
					memmove(&block.names[nameoff], &block.names[nameoff+namesize+1], NAMESPACE_PER_DIR_BLOCK - (nameoff+namesize+1));

					block.numbers[ENTRIES_PER_DIR_BLOCK - 1] = INO_EOF;
					memset(&block.names[NAMESPACE_PER_DIR_BLOCK - namesize - 1], 0, namesize + 1);
					assert(inode_write(disk, directory, pos, &block, sizeof(block)) == sizeof(block));
				}
				return res;
			}

			nameoff += curlen + 1;
			empty_count = -1;
		}

		pos += sizeof(block);
		empty_count++;
	}

	return -1;
}
