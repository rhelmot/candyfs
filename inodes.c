#include "storage_structures.h"

#include <time.h>

void now(struct timespec ts) {
	int res = clock_gettime(&ts, CLOCK_REALTIME);
	if (res < 0) {
		memset(ts, 0, sizeof(ts));
	}
}

// these are block counts, not byte counts
#define SINGLE_INDIRECT_COUNT ((int)(BLOCKSIZE / sizeof(blockno_t)))
#define DOUBLE_INDIRECT_COUNT (SINGLE_INDIRECT_COUNT * SINGLE_INDIRECT_COUNT)
#define TRIPLE_INDIRECT_COUNT (SINGLE_INDIRECT_COUNT * SINGLE_INDIRECT_COUNT * SINGLE_INDIRECT_COUNT)

// portion of inode which is not variable-length, so that we can calculate its size independently
#define INODE_HEAD \
	int magic;                          \
	mode_t mode;                        \
	nlink_t nlinks;                     \
	uid_t owner;                        \
	gid_t group;                        \
	off_t size;                         \
	struct timespec created;            \
	struct timespec last_access;        \
	struct timespec last_change;        \
	struct timespec last_statchange;

// number of pointer slots available in the block without the fixed length part
#define NUM_BLOCK_SLOTS ((BLOCKSIZE - sizeof(struct { INODE_HEAD })) / sizeof(blockno_t)) 

// configuration paramters for indirect counts
#define NUM_SINGLE_INDIRECT_SLOTS 1
#define NUM_DOUBLE_INDIRECT_SLOTS 1
#define NUM_TRIPLE_INDIRECT_SLOTS 1
#define NUM_DIRECT_SLOTS (NUM_BLOCK_SLOTS - NUM_SINGLE_INDIRECT_SLOTS - NUM_DOUBLE_INDIRECT_SLOTS - NUM_TRIPLE_INDIRECT_SLOTS)

// compute a bunch of slot indexes corresponding to various levels of indirection
#define FIRST_SINGLE_INDIRECT_SLOT NUM_DIRECT_SLOTS
#define FIRST_DOUBLE_INDIRECT_SLOT (FIRST_SINGLE_INDIRECT_SLOT + NUM_SINGLE_INDIRECT_SLOTS)
#define FIRST_TRIPLE_INDIRECT_SLOT (FIRST_DOUBLE_INDIRECT_SLOT + NUM_DOUBLE_INDIRECT_SLOTS)
#define FIRST_UNREACHABLE_SLOT     (FIRST_TRIPLE_INDIRECT_SLOT + NUM_TRIPLE_INDIRECT_SLOTS)
_Static_assert(FIRST_UNREACHABLE_SLOT == NUM_BLOCK_SLOTS)

// compute a bunch of block numbers correspondingn to various levels of indirection
#define FIRST_SINGLE_INDIRECT_BLOCK NUM_DIRECT_SLOTS
#define FIRST_DOUBLE_INDIRECT_BLOCK FIRST_SINGLE_INDIRECT_BLOCK + SINGLE_INDIRECT_COUNT
#define FIRST_TRIPLE_INDIRECT_BLOCK FIRST_DOUBLE_INDIRECT_BLOCK + DOUBLE_INDIRECT_COUNT
#define FIRST_UNREACHABLE_BLOCK FIRST_TRIPLE_INDIRECT_BLOCK + TRIPLE_INDIRECT_COUNT

// the actual structures present on disk!
typedef struct inode {
	INODE_HEAD
	blockno_t blocks[NUM_BLOCK_SLOTS];
} inode_t;

typedef blockno_t indirect_block_t[SINGLE_INDIRECT_COUNT];

_Static_assert(sizeof(inode_t) == BLOCKSIZE, "inode is not blocksize");
_Static_assert(sizeof(indirect_block_t) == BLOCKSIZE, "indirect block is not blocksize")

// various conversion functions between file offsets, block indexes, block slots, and indirection levels
// (block slot = index into inode.blocks)

// block index for a given file offset
inline long offset2blockidx(off_t off) {
	return off / BLOCKSIZE;
}

// indirection level under which a given block index will be buried
inline int indirection_level(long blockidx) {
	return blockidx < FIRST_SINGLE_INDIRECT_BLOCK ? 0 :
		blockidx < FIRST_DOUBLE_INDIRECT_BLOCK ? 1 :
		blockidx < FIRST_TRIPLE_INDIRECT_BLOCK ? 2 :
		blockidx < FIRST_UNREACHABLE_BLOCK ? 3 :
		-1;
}

// indirection level a given block slot holds
inline long blockslot_indirection_level(int blockslot) {
	return blockslot < FIRST_SINGLE_INDIRECT_SLOT ? 0 :
		blockslot < FIRST_DOUBLE_INDIRECT_SLOT ? 1 :
		blockslot < FIRST_TRIPLE_INDIRECT_SLOT ? 2 :
		blockslot < FIRST_UNREACHABLE_SLOT ? 3 :
		-1;
}

// number of data blocks represented by a single block of the given indireciton level
inline long indirect_count(int level) {
	switch (level) {
		case 0: return 1;
		case 1: return SINGLE_INDIRECT_COUNT;
		case 2: return DOUBLE_INDIRECT_COUNT;
		case 3: return TRIPLE_INDIRECT_COUNT;
		default: return 0;
	}
}

// the block slot which contains the given block index
inline int blockidx2blockslot(long blockidx) {
	switch (indirection_level(blockidx)) {
		case 0: return (int)blockidx;
		case 1: return FIRST_SINGLE_INDIRECT_SLOT + (int)((blockidx - FIRST_SINGLE_INDIRECT_BLOCK) / SINGLE_INDIRECT_COUNT);
		case 2: return FIRST_DOUBLE_INDIRECT_SLOT + (int)((blockidx - FIRST_DOUBLE_INDIRECT_BLOCK) / DOUBLE_INDIRECT_COUNT);
		case 3: return FIRST_TRIPLE_INDIRECT_SLOT + (int)((blockidx - FIRST_TRIPLE_INDIRECT_BLOCK) / TRIPLE_INDIRECT_COUNT);
		default: return -1;
	}
}

// the first block index held in the given block slot
inline long blockslot2firstblockidx(int blockslot) {
	switch (blockslot_indirection_level(blockslot)) {
		case 0: return (long)blockslot;
		case 1: return FIRST_SINGLE_INDIRECT_BLOCK + (long)((blockslot - FIRST_SINGLE_INDIRECT_SLOT) * SINGLE_INDIRECT_COUNT);
		case 2: return FIRST_DOUBLE_INDIRECT_BLOCK + (long)((blockslot - FIRST_DOUBLE_INDIRECT_SLOT) * DOUBLE_INDIRECT_COUNT);
		case 3: return FIRST_TRIPLE_INDIRECT_BLOCK + (long)((blockslot - FIRST_TRIPLE_INDIRECT_SLOT) * TRIPLE_INDIRECT_COUNT);
		default: return -1;
	}
}

// EXPORTED: allocate an inode. has one link by default.
ino_t inode_allocate(fakedisk_t *disk, mode_t mode, uid_t owner, gid_t group) {
	// set basic metadata
	inode_t inode;
	inode.magic = INODE_MAGIC;
	inode.mode = mode;
	inode.nlinks = 1;
	inode.owner = owner;
	inode.group = group;
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
	ino_t inumber = allocate_inumber(disk);
	if (inumber < 0) {
		return -1;
	}
	blockno_t block = allocate_block(disk);
	if (block < 0) {
		free_inumber(inumber);
		return -1;
	}

	// commit changes
	inumber_set_blocknumber(disk, inumber, block);
	write_block(disk, block, &inode);
	return inumber;
}

// EXPORTED: increase the number of links on an inode
void inode_link(fakedisk_t *disk, ino_t inumber) {
	blockno_t block = inumber_to_blocknumber(disk, inumber);
	inode_t inode;
	read_block(disk, block, &inode);
	inode.nlinks++;
	write_block(disk, block, &inode);
}

// EXPORTED: decrease the number of links on an inode. if it falls to 0, free it.
void inode_unlink(fakedisk_t *disk, ino_t inumber) {
	blockno_t block = inumber_to_blocknumber(disk, inumber);
	inode_t inode;
	read_block(disk, block, &inode);
	inode.nlinks--;

	if (inode.nlinks > 0) {
		write_block(disk, block, &inode);
	} else {
		inode_setsize(disk, inumber, 0);
		free_inumber(disk, inumber);
		free_block(disk, block)
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

bool inode_indirect_grow(fakedisk_t *disk, blockno_t *dest, long curblock, int indirection, long old_blockcount, long new_blockcount, long *allocated) {
	// load or initialize block
	// if necessary, allocate the next data (or indirect) block
	blockno_t blockno = *dest;
	indirect_block_t indirect_data;
	if (blockno == BLOCKNO_EOF) {
		blockno = allocate_block(disk);
		if (blockno < 0) {
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
			read_block(disk, blockno, indirect_data);
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
	if (curblock < orig_blockcount) {
		start_idx += (orig_blockcount - curblock) / sub_count;
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
		free_block(disk, blockno);
		*dest = BLOCKNO_EOF;
		*allocated = 0;
		return false;
	}
	*allocated = sum;
	write_block(disk, blockno, indirect_data);
	return success;
}

// set the size of an inode
off_t inode_setsize(fakedisk_t *disk, ino_t inumber, off_t size) {
	blockno_t block = inumber_to_blocknumber(disk, inumber);
	inode_t inode;
	read_block(disk, block, &inode);

	// convert sizes to block counts. we live in this world for the rest of the function
	long new_blockcount = offset2blockidx(size) + (size % BLOCKSIZE != 0);
	long old_blockcount = offset2blockidx(inode.size) + (inode.size % BLOCKSIZE != 0);
	if (new_blockcount < 0) {
		return -1;
	}

	long inode_blockcount = old_blockcount;
	bool success = true;
	int last_slot = -1;

	// loop until we have allocated enough space
	while (inode_blockcount < new_blockcount && success) {
		// compute which slot under which we should be allocating
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

	while (old_blockcount > new_blockcount) {
		// TODO: shrink algorithm
	}

	// error handling
	// if we didn't allocate enough make sure we set the size to a valid value
	if (inode_blockcount == new_blockcount) {
		inode.size = size;
		write_block(disk, block, &inode);
	} else {
		inode.size = inode_blockcount * BLOCKSIZE;
		write_block(disk, block, &inode);
	}
	return inode.size;
}
