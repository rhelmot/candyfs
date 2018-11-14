/**
 * Filesystem on RAM, CandyFS
 * 
 * Authors:
 *
 * This is a FS that uses RAM in order to create and store files using FUSE to access the Kernel filesystem calls
 * 
 * License: Fire in the hole :)
 */

#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "storage_emulator.h"



static int candy_getattr( const char *path, struct stat *st )
{
	printf( "Call: getattr() - path = %s\n", path );
	
	// Definitions of attributes:
    // Reference Document: http://www.gnu.org/software/libc/manual/html_node/Attribute-Meanings.html
	// st_uid: User ID of the owner of the file
	// st_gid: Group ID of the file.
	// st_atime: Last access time for the file.
	// st_mtime: Time of the last modification.
	// st_mode: Specifies the mode of the file. This includes file type information (see Testing File Type) and the file permission bits (see Permission Bits).
	// st_nlink: The number of hard links to the file. This count keeps track of how many directories have entries for this file.
    //           If the count goes to zero, then the file should be deleted 
    //           Symbolic links are not counted in the total.
	// st_size: Size of a regular file in bytes. 
    //          For files that are devices this field is not used. 
    //          For symbolic links, specifies the length of the file name the link refers to.
	
	st->st_uid = getuid(); // user who mounted the filesystem (valid for everything, files and directories)
	st->st_gid = getgid(); // group of the user who mounted the filesystem (valid for files and directories)
	st->st_atime = time( NULL ); // now
	st->st_mtime = time( NULL ); // now
	
	if ( strcmp( path, "/" ) == 0 )
	{
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2; // for . and ..
	}
	else
	{
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		st->st_size = 10; //** TO-DO ** temporal static value, assign 1K bytes to the file
	}
	
	return 0;
}


static int candy_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi )
{
	printf( "Call: readdir() - path = %s\n", path );
	
	filler( buffer, ".", NULL, 0 ); 
	filler( buffer, "..", NULL, 0 ); 
	
    //dummy implementation
    //**TO-DO**
	if ( strcmp( path, "/" ) == 0 ) // If the user is trying to show the files/directories of the root directory show the following
	{
		filler( buffer, "test1.txt", NULL, 0 );
		filler( buffer, "test2.txt", NULL, 0 );
        filler( buffer, "test3.txt", NULL, 0 );
	}
	
	return 0;
}



static int candy_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
    printf( "Call: read() - path = %s - offset = %li - size = %li\n", path, offset, size );

	char file_1_text[] = "Test candy file 1";
	char file_2_text[] = "Test candy file 2";
    char file_3_text[] = "Test candy file 3";
    
	char *text_in_file = NULL;
	
	// **TO-DO** //
	
	if ( strcmp( path, "/test1" ) == 0 )
		text_in_file = file_1_text;
	else if ( strcmp( path, "/test2" ) == 0 )
		text_in_file = file_2_text;
    else if ( strcmp( path, "/test3" ) == 0 )
        text_in_file = file_3_text;
	else
		return -1;
	
	// ... //
	
	memcpy( buffer, text_in_file + offset, size );
		
	return strlen( text_in_file ) - offset;
}





static int candy_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    //this implementation works with a file on disk, need to modify to use RAM
    int fd;
    int res;
    (void) fi;
    printf( "Call: write() - path = %s - offset = %li - size = %li\n", path, offset, size );

    if(fi == NULL)
    {
        fd = open(path, O_WRONLY);
    }
    else
    {
        fd = fi->fh;
    }
    if (fd == -1)
    {
        return - errno;
    }

    res = pwrite(fd, buf, size, offset);
    if (res == -1){
        res = - errno;
    }

    if(fi == NULL)
    {
        close(fd);
    }
    return res;
}


/**
 * this structure maps the calls of FUSE to the local implementations on CandyFS
 * Doc: https://libfuse.github.io/doxygen/passthrough_8c.html
 */

static struct fuse_operations operations = {
    .getattr	= candy_getattr,
    .readdir	= candy_readdir,
    .read		= candy_read,
    .write      = candy_write,
};



int main(int argc, char *argv[]) {
    printf("---------------------------------------------------------\n");
    printf("!                      candyFS                          !\n");
    printf("---------------------------------------------------------\n");
    printf("Use -f to see debug messages\n");
    printf("Example: ./candyfs -f ./mpoint\n");
    printf("---------------------------------------------------------\n");
    printf("To unmount execute: fusermount -u [mount_point_directory]\n");
    return fuse_main( argc, argv, &operations, NULL );
}