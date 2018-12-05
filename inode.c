#include "inode.h"

#include <time.h>
#include <string.h>
#include <stdbool.h>

void now(struct timespec *ts) {
	int res = clock_gettime(CLOCK_REALTIME, ts);
	if (res < 0) {
		memset(ts, 0, sizeof(*ts));
	}
}

// these are block counts, not byte counts
#define SINGLE_INDIRECT_COUNT ((long)(BLOCKSIZE / sizeof(blockno_t)))
#define DOUBLE_INDIRECT_COUNT (SINGLE_INDIRECT_COUNT * SINGLE_INDIRECT_COUNT)
#define TRIPLE_INDIRECT_COUNT (SINGLE_INDIRECT_COUNT * SINGLE_INDIRECT_COUNT * SINGLE_INDIRECT_COUNT)

// portion of inode which is not variable-length, so that we can calculate its size independently
#define INODE_MAGIC 0xCA4140DE
#define INODE_HEAD \
	INODE_META \
	unsigned int magic;

// number of pointer slots available in the block without the fixed length part
#define NUM_BLOCK_SLOTS ((long)((BLOCKSIZE - sizeof(struct { INODE_HEAD })) / sizeof(blockno_t)) )

// configuration paramters for indirect counts
#define NUM_SINGLE_INDIRECT_SLOTS 1
#define NUM_DOUBLE_INDIRECT_SLOTS 1
#define NUM_TRIPLE_INDIRECT_SLOTS 1
#define NUM_DIRECT_SLOTS (NUM_BLOCK_SLOTS - NUM_SINGLE_INDIRECT_SLOTS - NUM_DOUBLE_INDIRECT_SLOTS - NUM_TRIPLE_INDIRECT_SLOTS)

// compute a bunch of slot indexes corresponding to various levels of indirection
#define FIRST_SINGLE_INDIRECT_SLOT (NUM_DIRECT_SLOTS)
#define FIRST_DOUBLE_INDIRECT_SLOT (FIRST_SINGLE_INDIRECT_SLOT + NUM_SINGLE_INDIRECT_SLOTS)
#define FIRST_TRIPLE_INDIRECT_SLOT (FIRST_DOUBLE_INDIRECT_SLOT + NUM_DOUBLE_INDIRECT_SLOTS)
#define FIRST_UNREACHABLE_SLOT     (FIRST_TRIPLE_INDIRECT_SLOT + NUM_TRIPLE_INDIRECT_SLOTS)
_Static_assert(FIRST_UNREACHABLE_SLOT == NUM_BLOCK_SLOTS, "not all block slots accounted for?");

// compute a bunch of block numbers correspondingn to various levels of indirection
#define FIRST_SINGLE_INDIRECT_BLOCK (NUM_DIRECT_SLOTS)
#define FIRST_DOUBLE_INDIRECT_BLOCK (FIRST_SINGLE_INDIRECT_BLOCK + SINGLE_INDIRECT_COUNT)
#define FIRST_TRIPLE_INDIRECT_BLOCK (FIRST_DOUBLE_INDIRECT_BLOCK + DOUBLE_INDIRECT_COUNT)
#define FIRST_UNREACHABLE_BLOCK (FIRST_TRIPLE_INDIRECT_BLOCK + TRIPLE_INDIRECT_COUNT)

#define MAX_FILESIZE (FIRST_UNREACHABLE_BLOCK * BLOCKSIZE)

// the actual structures present on disk!
typedef struct inode {
	INODE_HEAD
	blockno_t blocks[NUM_BLOCK_SLOTS];
} inode_t;

typedef blockno_t indirect_block_t[SINGLE_INDIRECT_COUNT];

_Static_assert(sizeof(inode_t) == BLOCKSIZE, "inode is not blocksize");
_Static_assert(sizeof(indirect_block_t) == BLOCKSIZE, "indirect block is not blocksize");

// various conversion functions between file offsets, block indexes, block slots, and indirection levels
// (block slot = index into inode.blocks)

// block index for a given file offset
long offset2blockidx(off_t off) {
	return off / BLOCKSIZE;
}

// indirection level under which a given block index will be buried
int indirection_level(long blockidx) {
	return blockidx < FIRST_SINGLE_INDIRECT_BLOCK ? 0 :
		blockidx < FIRST_DOUBLE_INDIRECT_BLOCK ? 1 :
		blockidx < FIRST_TRIPLE_INDIRECT_BLOCK ? 2 :
		blockidx < FIRST_UNREACHABLE_BLOCK ? 3 :
		-1;
}

// indirection level a given block slot holds
long blockslot_indirection_level(int blockslot) {
	return blockslot < FIRST_SINGLE_INDIRECT_SLOT ? 0 :
		blockslot < FIRST_DOUBLE_INDIRECT_SLOT ? 1 :
		blockslot < FIRST_TRIPLE_INDIRECT_SLOT ? 2 :
		blockslot < FIRST_UNREACHABLE_SLOT ? 3 :
		-1;
}

// number of data blocks represented by a single block of the given indireciton level
long indirect_count(int level) {
	switch (level) {
		case 0: return 1;
		case 1: return SINGLE_INDIRECT_COUNT;
		case 2: return DOUBLE_INDIRECT_COUNT;
		case 3: return TRIPLE_INDIRECT_COUNT;
		default: return 0;
	}
}

// the block slot which contains the given block index
int blockidx2blockslot(long blockidx) {
	switch (indirection_level(blockidx)) {
		case 0: return (int)blockidx;
		case 1: return FIRST_SINGLE_INDIRECT_SLOT + (int)((blockidx - FIRST_SINGLE_INDIRECT_BLOCK) / SINGLE_INDIRECT_COUNT);
		case 2: return FIRST_DOUBLE_INDIRECT_SLOT + (int)((blockidx - FIRST_DOUBLE_INDIRECT_BLOCK) / DOUBLE_INDIRECT_COUNT);
		case 3: return FIRST_TRIPLE_INDIRECT_SLOT + (int)((blockidx - FIRST_TRIPLE_INDIRECT_BLOCK) / TRIPLE_INDIRECT_COUNT);
		default: return -1;
	}
}

// the first block index held in the given block slot
long blockslot2firstblockidx(int blockslot) {
	switch (blockslot_indirection_level(blockslot)) {
		case 0: return (long)blockslot;
		case 1: return FIRST_SINGLE_INDIRECT_BLOCK + (long)((blockslot - FIRST_SINGLE_INDIRECT_SLOT) * SINGLE_INDIRECT_COUNT);
		case 2: return FIRST_DOUBLE_INDIRECT_BLOCK + (long)((blockslot - FIRST_DOUBLE_INDIRECT_SLOT) * DOUBLE_INDIRECT_COUNT);
		case 3: return FIRST_TRIPLE_INDIRECT_BLOCK + (long)((blockslot - FIRST_TRIPLE_INDIRECT_SLOT) * TRIPLE_INDIRECT_COUNT);
		default: return -1;
	}
}

// recursive algorithm for allocating space for a file
// called for each block (indirect or data!) so that it (and its children!)
// can be allocated. The first several parameters identify the current location we are
// working on, and the last two identify the allocation job we're trying to complete.
//
// parameters:
//   disk: the disk
//   dest: in/outparam. a pointer to where the current block's disk pointer should be/is
//         stored. if it is BLOCKNO_EOF it means we need to allocate the current block
//   allocated: outparam. the actual number of blocks allocated
//   curblock: the block index of the first data block represented in this block
//   indirection: the level of indirection we're currently working at. 3 means the current
//         subject is a triple-indirect block, 0 means the current subject is a data block.
//   old_blockcount: the old number of allocated data blocks for the file
//   new_blockcount: the new number of allocated data blocks we're trying to reach for the file
//
// returns whether the operation succeeded. if it failed, it may have still allocated something.
//

bool inode_indirect_grow(disk_t *disk, blockno_t *dest, long curblock, int indirection, long old_blockcount, long new_blockcount, long *allocated) {
	// load or initialize block
	// if necessary, allocate the next data (or indirect) block
	blockno_t blockno = *dest;
	indirect_block_t indirect_data;
	if (blockno == BLOCKNO_EOF) {
		blockno = block_allocate(disk);
		if ((long)blockno < 0) {
			*allocated = 0;
			return false;
		}
		*dest = blockno;

		// initialize the new indirect block
		if (indirection != 0) {
			for (int i = 0; i < SINGLE_INDIRECT_COUNT; i++) {
				indirect_data[i] = BLOCKNO_EOF;
			}
		}
	} else {
		blockno = *dest;

		// load the block
		// if we get here for a data block there's a serious issue
		if (indirection != 0) {
			disk_read(disk, blockno, indirect_data);
		} else {
			assert(false);
		}
	}

	// if we're working with a data block we obviously don't need any of the below
	if (indirection == 0) {
		*allocated = 1;
		return true;
	}

	/* old version of the algorithm back when I was a dumbass and forgot outparams exist
	// compute some basic stats we'll need a bunch
	long sub_count = indirect_count(indirection - 1);
	long endblock = curblock + SINGLE_INDIRECT_COUNT * sub_count;

	// compute exactly how much we need to fill
	// also how much will be missing from the first/last children's sums
	long sum_target = endblock - curblock;
	int start_idx = 0;
	int end_idx = SINGLE_INDIRECT_COUNT - 1; // this is inclusive
	long difference_start = 0;
	long difference_end = 0;

	// this is absolutely the most fraught with
	// potential for off-by-one errors I've ever seen
	if (curblock < orig_blockcount) {
		sum_target -= orig_blockcount - curblock;
		start_idx += (orig_blockcount - curblock) / sub_count;
		difference_start = (orig_blockcount - curblock) % sub_count;
	}
	if (endblock > new_blockcount) {
		sum_target -= endblock - new_blockcount;
		end_idx -= (endblock - new_blockcount) / sub_count;
		difference_end = (endblock - new_blockcount) % sub_count;
	}
	*/

	// do some clerical work to figure out the most efficient range over which to recurse
	long sub_count = indirect_count(indirection - 1);
	long endblock = curblock + SINGLE_INDIRECT_COUNT * sub_count;
	int start_idx = 0;
	int end_idx = SINGLE_INDIRECT_COUNT - 1; // this is inclusive
	// hillariously fraught with potential for off-by-one errors
	if (curblock < old_blockcount) {
		start_idx += (old_blockcount - curblock) / sub_count;
	}
	if (endblock > new_blockcount) {
		end_idx -= (endblock - new_blockcount) / sub_count;
	}

	// actually do the recursion. for each in-range child, run this function
	// add up how many block were actually allocated
	// if anyone ever reports an error, return early
	long sum = 0;
	bool success = true;
	for (int i = start_idx; i <= end_idx && success; i++) {
		long added;
		success = inode_indirect_grow(
				disk,
				&indirect_data[i],
				curblock + sub_count * i,
				indirection - 1,
				old_blockcount,
				new_blockcount,
				&added
		);
		sum += added;
	}

	if (!success && start_idx == 0 && sum == 0) {
		// HYPERDEATH EDGE CASE
		// if we have failed to allocate ANY data to this NEW indirect block, we must
		// free the indirect block and clear its pointer. otherwise, it will be excluded
		// by the new compted filesize and not be touched in e.g. the shrink algorithm
		block_free(disk, blockno);
		*dest = BLOCKNO_EOF;
		*allocated = 0;
		return false;
	}
	*allocated = sum;
	disk_write(disk, blockno, indirect_data);
	return success;
}

// shrink the allocated space of a file. Structured very similarly to inode_indirect_grow,
// so look at that for detailed information.
void inode_indirect_shrink(disk_t *disk, blockno_t *dest, long curblock, int indirection, long old_blockcount, long new_blockcount, long *freed) {
	blockno_t blockno = *dest;
	assert(blockno != BLOCKNO_EOF);

	if (indirection == 0) {
		block_free(disk, blockno);
		*dest = BLOCKNO_EOF;
		*freed = 1;
		return;
	}

	indirect_block_t indirect_data;
	disk_read(disk, blockno, indirect_data);

	// do some clerical work to figure out the most efficient range over which to recurse
	long sub_count = indirect_count(indirection - 1);
	long endblock = curblock + SINGLE_INDIRECT_COUNT * sub_count;
	int start_idx = 0;
	int end_idx = SINGLE_INDIRECT_COUNT - 1; // this is inclusive
	// hillariously fraught with potential for off-by-one errors
	if (curblock < new_blockcount) {
		start_idx += (new_blockcount - curblock) / sub_count;
	}
	if (endblock > old_blockcount) {
		end_idx -= (endblock - old_blockcount) / sub_count;
	}

	// loop over everything in-range and do the frees!
	// keep track of the total number of blocks freed and whether anything has been
	// not yet freed
	long sum = 0;
	for (int i = start_idx; i <= end_idx; i++) {
		long removed;
		inode_indirect_shrink(
			disk,
			&indirect_data[i],
			curblock + i * sub_count,
			indirection - 1,
			old_blockcount,
			new_blockcount,
			&removed
		);
		sum += removed;
	}

	// if we really have freed everything, free ourselves
	// otherwise, write back the changed block
	bool everything = true;
	for (int i = 0; everything && i < SINGLE_INDIRECT_COUNT; i++) {
		everything = indirect_data[i] == BLOCKNO_EOF;
	}
	if (everything) {
		block_free(disk, blockno);
		*dest = BLOCKNO_EOF;
	} else {
		disk_write(disk, blockno, indirect_data);
	}
	*freed = sum;
	return;
}

// data may be NULL indicating it is all zeros
ssize_t inode_indirect_readwrite(disk_t *disk, blockno_t blockno, long curblock, int indirection, off_t pos, off_t endpos, void *data, bool write) {
	assert(blockno != BLOCKNO_EOF);

	if (indirection == 0) {
		data_block_t block;
		off_t blockpos = curblock * BLOCKSIZE;
		long block_delta, data_delta;
		ssize_t copy_size = BLOCKSIZE;
		block_delta = blockpos - pos;
		if (blockpos < pos) {
			data_delta = 0;
			block_delta = pos - blockpos;
			copy_size -= block_delta;
		} else {
			data_delta = blockpos - pos;
			block_delta = 0;
		}

		if (endpos < blockpos + BLOCKSIZE) {
			copy_size -= (blockpos + BLOCKSIZE) - endpos;
		}

		if (!write) {
			disk_read(disk, blockno, block);
			memcpy(data + data_delta, &block[block_delta], copy_size);
		} else {
			if (copy_size != BLOCKSIZE) {
				disk_read(disk, blockno, block);
			}
			if (data != NULL) {
				memcpy(&block[block_delta], data + data_delta, copy_size);
			} else {
				memset(&block[block_delta], 0, copy_size);
			}
			disk_write(disk, blockno, block);
		}
		return copy_size;
	}

	indirect_block_t indirect_data;
	disk_read(disk, blockno, indirect_data);

	// do some clerical work to figure out the most efficient range over which to recurse
	long sub_count = indirect_count(indirection - 1);
	long endblock = curblock + SINGLE_INDIRECT_COUNT * sub_count - 1; // inclusive?
	long first_block = pos / BLOCKSIZE;
	long last_block = (endpos - 1) / BLOCKSIZE; // inclusive?
	int start_idx = 0;
	int end_idx = SINGLE_INDIRECT_COUNT - 1; // this is inclusive
	// hillariously fraught with potential for off-by-one errors
	if (curblock < first_block) {
		start_idx += (first_block - curblock) / sub_count;
	}
	if (endblock > last_block) {
		end_idx -= (endblock - last_block) / sub_count;
	}

	ssize_t result = 0;
	for (int i = start_idx; i <= end_idx; i++) {
		result += inode_indirect_readwrite(
			disk,
			indirect_data[i],
			curblock + i * sub_count,
			indirection - 1,
			pos,
			endpos,
			data,
			write
		);
	}
	return result;
}

// set the size of an inode
off_t inode_setsize(disk_t *disk, ino_t inumber, off_t size) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	// quick cap: don't go past max filesize
	if (size > MAX_FILESIZE) {
		size = MAX_FILESIZE;
	}

	// convert sizes to block counts. we live in this world for the rest of the function
	long new_blockcount = offset2blockidx(size) + (size % BLOCKSIZE != 0);
	long old_blockcount = offset2blockidx(inode.size) + (inode.size % BLOCKSIZE != 0);
	if (new_blockcount < 0) {
		return -1;
	}

	long inode_blockcount = old_blockcount;
	bool success = true;
	int last_slot = -1;

	// if we need to grow: loop until we have allocated enough space
	while (inode_blockcount < new_blockcount && success) {
		// compute the slot under which we should be allocating
		long new_blockidx = inode_blockcount;
		int indirection = indirection_level(new_blockidx);
		int slot = blockidx2blockslot(new_blockidx);
		long curblock = blockslot2firstblockidx(slot);

		// assert that each round touches a sequencial slot
		assert(last_slot == -1 || last_slot + 1 == slot);
		last_slot = slot;

		// allocate!
		long added;
		success = inode_indirect_grow(
			disk,
			&inode.blocks[slot],
			curblock,
			indirection,
			old_blockcount,
			new_blockcount,
			&added
		);
		inode_blockcount += added;
	}

	// if we need to shrink: loop until we have freed enough space
	while (inode_blockcount > new_blockcount) {
		// compute the slot under which we should be freeing
		long final_blockidx = inode_blockcount - 1;
		int indirection = indirection_level(final_blockidx);
		int slot = blockidx2blockslot(final_blockidx);
		long curblock = blockslot2firstblockidx(slot);

		// assert that each round touches a sequencial slot
		assert(last_slot == -1 || last_slot - 1 == slot);
		last_slot = slot;

		// free!
		long freed;
		inode_indirect_shrink(
			disk,
			&inode.blocks[slot],
			curblock,
			indirection,
			old_blockcount,
			new_blockcount,
			&freed
		);
		inode_blockcount -= freed;
	}


	// error handling
	// if we didn't allocate enough make sure we set the size to a valid value
	off_t oldsize = inode.size;
	if (inode_blockcount == new_blockcount) {
		inode.size = size;
	} else {
		inode.size = inode_blockcount * BLOCKSIZE;
	}

	// update timestamps and flush
	if (inode.size != oldsize) {
		now(&inode.last_statchange);
		inode.last_change = inode.last_statchange;
	}
	disk_write(disk, block, &inode);
	return inode.size;
}

// EXPORTED: allocate an inode. zero links, full permissions, and owned by root by default.
ino_t inode_allocate(disk_t *disk) {
	// set basic metadata
	inode_t inode;
	inode.magic = INODE_MAGIC;
	inode.mode = 0777;
	inode.nlinks = 0;
	inode.owner = 0;
	inode.group = 0;
	inode.size = 0;
	now(&inode.created);
	inode.last_access = inode.created;
	inode.last_change = inode.created;
	inode.last_statchange = inode.created;

	// all blocks are empty by default
	for (int i = 0; i < NUM_BLOCK_SLOTS; i++) {
		inode.blocks[i] = BLOCKNO_EOF;
	}

	// allocate resources. if anything fails, clean up and abort
	ino_t inumber = ino_allocate(disk);
	if ((long)inumber < 0) {
		return -1;
	}
	blockno_t block = block_allocate(disk);
	if ((long)block < 0) {
		ino_free(disk, inumber);
		return -1;
	}

	// commit changes
	ino_set(disk, inumber, block);
	disk_write(disk, block, &inode);
	return inumber;
}

// EXPORTED: free an inode. will fail if there are any links to it.
int inode_free(disk_t *disk, ino_t inumber) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1; // CRITICAL ERROR
	}

	// is this appropriate? probably an okay sanity check...
	if (inode.nlinks != 0) {
		return -1;
	}

	inode_setsize(disk, inumber, 0);
	ino_free(disk, inumber);
	block_free(disk, block);
	return 0;
}

// EXPORTED: set the mode field atomicly
int inode_chmod(disk_t *disk, ino_t inumber, mode_t mode) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	inode.mode = mode;
	now(&inode.last_statchange);
	disk_write(disk, block, &inode);
	return 0;
}

// EXPORTED: set the uid/gid fields atomicly
int inode_chown(disk_t *disk, ino_t inumber, uid_t owner, gid_t group) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	if (owner != (unsigned int)~0) {
		inode.owner = owner;
	}
	if (group != (unsigned int)~0) {
		inode.group = group;
	}
	now(&inode.last_statchange);
	disk_write(disk, block, &inode);
	return 0;
}

// EXPORTED: get the inode metadata
int inode_getinfo(disk_t *disk, ino_t inumber, inode_info_t *info) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	memcpy(info, &inode, sizeof(inode_info_t));
	return 0;
}

// EXPORTED: set the atime/mtime fields
int inode_utime(disk_t *disk, ino_t inumber, const struct timespec *last_access, const struct timespec *last_change) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	now(&inode.last_statchange);
	if (last_access == NULL || last_access->tv_nsec == UTIME_NOW) {
		inode.last_access = inode.last_statchange;
	} else if (last_access->tv_nsec != UTIME_OMIT) {
		inode.last_access = *last_access;
	}
	if (last_change == NULL || last_change->tv_nsec == UTIME_NOW) {
		inode.last_change = inode.last_statchange;
	} else if (last_change->tv_nsec != UTIME_OMIT) {
		inode.last_change = *last_change;
	}

	disk_write(disk, block, &inode);
	return 0;
}

// EXPORTED: atomically increment the link count
nlink_t inode_link(disk_t *disk, ino_t inumber) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	inode.nlinks++;
	now(&inode.last_statchange);
	disk_write(disk, block, &inode);
	return inode.nlinks;
}

// EXPORTED: atomically decrement the link count. does not handle freeing at 0 links
nlink_t inode_unlink(disk_t *disk, ino_t inumber) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	inode.nlinks--;
	now(&inode.last_statchange);
	disk_write(disk, block, &inode);
	return inode.nlinks;
}

// EXPORTED: write to a file
// if pos is -1 this is an atomic append
ssize_t inode_write(disk_t *disk, ino_t inumber, off_t pos, const void *data, ssize_t size) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	if (pos == -1) {
		pos = inode.size;
	}

	off_t endpos = pos + size;
	off_t zero_endpos = pos;

	// extend the file if it would go past the end
	if (endpos > inode.size) {
		// figure out what we need to zero-fill, if anything
		if (pos > inode.size) {
			zero_endpos = pos;
			pos = inode.size;
		}

		// complicated: the inode will be mutated by this opreration
		// (notably the block slots) so we have to reload it!
		if ((long)inode_setsize(disk, inumber, endpos) < 0) {
			return -1;
		}
		disk_read(disk, block, &inode);

		assert(endpos >= inode.size);
		if (endpos > inode.size) {
			// RAN OUT OF ROOM WAHP WAHP
			// we should still complete as much of the write as possible
			endpos = inode.size;
			if (zero_endpos < inode.size) {
				zero_endpos = inode.size;
			}
		}
	}

	// stop early if this is a null write
	if (endpos <= pos) {
		return 0;
	}

	// loop until we've written to the specified end-point
	off_t curpos = pos;
	int last_slot = -1;
	while (curpos < endpos) {
		// compute the slot under which we should be writing
		long cur_blockidx = offset2blockidx(curpos);
		int indirection = indirection_level(cur_blockidx);
		int slot = blockidx2blockslot(cur_blockidx);
		long curblock = blockslot2firstblockidx(slot);

		// assert that each round touches a sequencial slot
		// if we've just crossed zero_endpos we may be in the same slot still
		assert(last_slot == -1 || curpos == zero_endpos || last_slot + 1 == slot);
		last_slot = slot;

		// write!
		// if we haven't passed zero_endpos write zeros
		curpos += inode_indirect_readwrite(
			disk,
			inode.blocks[slot],
			curblock,
			indirection,
			curpos < zero_endpos ? pos         : zero_endpos,
			curpos < zero_endpos ? zero_endpos : endpos,
			curpos < zero_endpos ? NULL        : (void*)data, // I PROMISE this cast is okay
			true
		);
	}

	now(&inode.last_change);
	disk_write(disk, block, &inode);

	assert(curpos == endpos);
	assert(endpos - zero_endpos >= 0);
	return endpos - zero_endpos;
}

// EXPORTED: read from a file
ssize_t inode_read(disk_t *disk, ino_t inumber, off_t pos, void *data, ssize_t size) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	off_t endpos = pos + size;

	// truncate the read if it would go past the end
	if (endpos > inode.size) {
		endpos = inode.size;
	}

	// stop early if this is a null read
	if (endpos <= pos) {
		return 0;
	}

	// read chunks from individual slots until we have read the appropriate amount
	off_t curpos = pos;
	int last_slot = -1;
	while (curpos < endpos) {
		// compute the slot under which we should be reading
		long cur_blockidx = offset2blockidx(curpos);
		int indirection = indirection_level(cur_blockidx);
		int slot = blockidx2blockslot(cur_blockidx);
		long curblock = blockslot2firstblockidx(slot);

		// assert that each round touches a sequencial slot
		assert(last_slot == -1 || last_slot + 1 == slot);
		last_slot = slot;

		// read!
		curpos += inode_indirect_readwrite(
			disk,
			inode.blocks[slot],
			curblock,
			indirection,
			pos,
			endpos,
			data,
			false
		);
	}

	now(&inode.last_access);
	disk_write(disk, block, &inode);

	assert(curpos == endpos);
	return endpos - pos;
}

// EXPORTED: pretty much just the ftruncate syscall. like inode_setsize but does zero-padding
off_t inode_truncate(disk_t *disk, ino_t inumber, off_t size) {
	blockno_t block = ino_get(disk, inumber);
	if ((long)block < 0) {
		return -1;
	}
	inode_t inode;
	disk_read(disk, block, &inode);
	if (inode.magic != INODE_MAGIC) {
		return -1;
	}

	off_t newsize = inode_setsize(disk, inumber, size);

	if (newsize > inode.size) {
		inode_write(disk, inumber, inode.size, NULL, newsize - inode.size);
	}

	return newsize;
}
