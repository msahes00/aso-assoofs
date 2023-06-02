/**
 * Include dependencies
 */
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// workaround for the timespec64
#define timespec64 timespec

#include "assoofs.h"

/**
 * Some constants
 */
#define WELCOMEFILE_WRITE           1                                   // Whether to write the welcome file or not
#define WELCOMEFILE_FILENAME        "README.txt"                        // The filename for the welcome file
#define WELCOMEFILE_BLOCK_NUMBER    (ASSOOFS_LAST_RESERVED_BLOCK + 1)   // The block number for the welcome file
#define WELCOMEFILE_INODE_NUMBER    (ASSOOFS_LAST_RESERVED_INODE + 1)   // The inode number for the welcome file

/**
 * Write the superblock to a file
 */
static int write_superblock(int fd) {
	
	ssize_t byte_count;

	// Create the superblock
	struct assoofs_super_block sb = {
		.magic = ASSOOFS_MAGIC,
		.version = ASSOOFS_VERSION,
		.block_size = ASSOOFS_BLOCK_SIZE,
		.inodes_count = ASSOOFS_LAST_RESERVED_INODE,
		.free_blocks = 0xFFFFFFFFFFFFFFF8
	};

	// Update the fields if the welcome file is present
	#if WELCOMEFILE_WRITE

		sb.free_blocks = 0xFFFFFFFFFFFFFFF0;
		sb.inodes_count = WELCOMEFILE_INODE_NUMBER;
	
	#endif

	// Write the superblock to the file and verify it
	printf("Writing the superblock\n");

	byte_count = write(fd, &sb, sizeof(sb));

	if (byte_count != ASSOOFS_BLOCK_SIZE) {

		printf("Malformed superblock: Disk block not filed (written %d bytes)\n", (int) byte_count);
		return -1;
	}

	printf("Super block written successfully\n");
	return 0;
}

/**
 * Write the root inode to a file
 */
static int write_root_inode(int fd) {

	ssize_t byte_count;

	// Create and populate the root inode
	struct assoofs_inode root_inode;
	root_inode.mode = S_IFDIR;
	root_inode.inode_no = ASSOOFS_ROOTDIR_INODE_NUMBER;
	root_inode.data_block_number = ASSOOFS_ROOTDIR_BLOCK_NUMBER;
	clock_gettime(CLOCK_REALTIME, &root_inode.time);

	// Set the children count correctly
	#if WELCOMEFILE_WRITE
		root_inode.dir_children_count = 1;
	#else
		root_inode.dir_children_count = 0;
	#endif

	// Write the root inode to file and check for errors
	printf("Writing the inode store\n");
	byte_count = write(fd, &root_inode, sizeof(root_inode));

	if (byte_count != sizeof(root_inode)) {

		printf("The inode store was not written properly\n");
		return -1;
	}

	printf("Inode store written successfully\n");
	return 0;
}

/**
 * Write the welcome inode
 */
static int write_welcome_inode(int fd, const struct assoofs_inode *i) {

	// TODO: UPDATE

	off_t nbytes;
	ssize_t ret;

	ret = write(fd, i, sizeof(*i));
	if (ret != sizeof(*i)) {
		printf("The welcome file inode was not written properly.\n");
		return -1;
	}
	printf("Welcome file inode written succesfully.\n");

	nbytes = ASSOOFS_BLOCK_SIZE - (sizeof(*i) * 2);
	ret = lseek(fd, nbytes, SEEK_CUR);
	if (ret == (off_t)-1) {
		printf("The padding bytes are not written properly.\n");
		return -1;
	}

	printf("Inode store padding bytes (after two inodes) written sucessfully.\n");
	return 0;
}

int write_dirent(int fd, const struct assoofs_dir_record_entry *record) {

	// TODO: UPDATE

	ssize_t nbytes = sizeof(*record), ret;

	ret = write(fd, record, nbytes);
	if (ret != nbytes) {
		printf("Writing the root directory datablock (name+inode_no pair for welcome file) has failed.\n");
		return -1;
	}
	printf("Root directory datablocks (name+inode_no pair for welcomefile) written succesfully.\n");

	nbytes = ASSOOFS_BLOCK_SIZE - sizeof(*record);
	ret = lseek(fd, nbytes, SEEK_CUR);
	if (ret == (off_t)-1) {
		printf("Writing the padding for root directory children datablock has failed.\n");
		return -1;
	}
	printf("Padding after the root directory children written succesfully.\n");
	return 0;
}

int write_block(int fd, char *block, size_t len) {

	// TODO: UPDATE
	ssize_t ret;

	ret = write(fd, block, len);
	if (ret != len) {
		printf("Writing file body has failed.\n");
		return -1;
	}
	printf("Block has been written succesfully.\n");
	return 0;
}

/**
 * Main
 */
int main(int argc, char *argv[]) {

	int fd;
	int code = -1;
	char welcomefile_content[] = "Hello world from " ASSOOFS_NAME;
	ssize_t welcomefile_size = sizeof(welcomefile_content) - 1; // dont write the "\0"
	
	struct assoofs_inode welcomefile_inode = {
		.mode = S_IFREG,
		.inode_no = WELCOMEFILE_INODE_NUMBER,
		.data_block_number = WELCOMEFILE_BLOCK_NUMBER,
		.file_size = welcomefile_size, 
	};
	
	struct assoofs_dir_record_entry welcomefile_record = {
		.filename = WELCOMEFILE_FILENAME,
		.inode_no = WELCOMEFILE_INODE_NUMBER,
	};

	// set the time
	clock_gettime(CLOCK_REALTIME, &welcomefile_inode.time);


	// Verify the parameters
	if (argc != 2) {
		printf("Usage: ./mkassoofs <device>\n");
		return code;
	}

	// Open the file for writing
	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		printf("Error opening the device\n");
		return code;
	}

	// Write the components of the filesystem to the file
	do {
		if (write_superblock(fd)) 
			break;

		if (write_root_inode(fd)) 
			break;
		
		if (write_welcome_inode(fd, &welcomefile_inode)) 
			break;

		if (write_dirent(fd, &welcomefile_record)) 
			break;
		
		if (write_block(fd, welcomefile_content, welcomefile_size)) 
			break;

		code = 0;
	} while (0);

	// Close the file and exit
	close(fd);
	return code;
}
