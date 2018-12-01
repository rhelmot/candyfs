COMMON_OBJECTS = disk.o block.o inode.o file.o dir.o symlink.o refs.o perm.o tree.o

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
