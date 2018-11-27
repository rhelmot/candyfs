COMMON_OBJECTS = storage_emulator.o storage_structures.o inodes.o directories.o tree.o

CFLAGS=`pkg-config fuse --cflags` -g -O0
LDFLAGS=`pkg-config fuse --libs`

%.o: %.c
	$(CC) -c $< $(CFLAGS) -o $@

candyfs: $(COMMON_OBJECTS) candyfs.o
	$(CC) $^ -o $@ $(LDFLAGS)

test: $(COMMON_OBJECTS) test.o
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f candyfs test *.o
