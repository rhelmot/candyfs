COMMON_OBJECTS = disk.o block.o inode.o file.o dir.o symlink.o refs.o perm.o path.o

CFLAGS=`pkg-config fuse --cflags` -g -O0 -Wall -Wpedantic
LDFLAGS=`pkg-config fuse --libs`

all: mount.candyfs mkfs.candyfs

%.o: %.c
	$(CC) -c $< $(CFLAGS) -o $@

mount.candyfs: $(COMMON_OBJECTS) candyfs.o
	$(CC) $^ -o $@ $(LDFLAGS)

mkfs.candyfs: $(COMMON_OBJECTS) mkfs.o
	$(CC) $^ -o $@ $(LDFLAGS)

test: $(COMMON_OBJECTS) test.o
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f mkfs.candyfs mount.candyfs test *.o
