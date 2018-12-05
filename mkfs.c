#include "disk.h"
#include "block.h"
#include "path.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void usage() {
	puts("mkfs.candyfs [options] device");
	puts("");
	puts("Options:");
	puts("  --user          Set root directory to be owned by current user");
	exit(1);
}

int main(int argc, char **argv) {
	bool user = false;
	char *device;
	if (argc > 3) {
		usage();
	} else if (argc == 3) {
		if (strcmp(argv[1], "--user") == 0) {
			user = true;
			device = argv[2];
		} else {
			usage();
		}
	} else if (argc == 2) {
		device = argv[1];
	} else {
		usage();
	}

	disk_t *disk = disk_open(device, BLOCKSIZE);
	mkfs_storage(disk, disk->nblocks / 256);

	if (user) {
		assert(mkfs_path(disk, getuid(), getgid()) == 0);
	} else {
		assert(mkfs_path(disk, 0, 0) == 0);
	}

	return 0;
}
