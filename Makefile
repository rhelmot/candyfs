COMPILER = gcc
FILESYSTEM_FILES = candyfs.c storage_emulator.c storage_structures.c

build: $(FILESYSTEM_FILES)
	$(COMPILER) $(FILESYSTEM_FILES) -o candyfs `pkg-config fuse --cflags --libs`


clean:
	rm candyfs