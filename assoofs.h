/**
 * Define some constants for the assoofs filesystem
 */
#define ASSOOFS_NAME    "assoofs"   // The filesystem name
#define ASSOOFS_MAGIC   0x20230602  // The magic code of the filesystem
#define ASSOOFS_VERSION 1           // The version of the filesystem

#define ASSOOFS_BLOCK_SIZE              4096    // The size of a block in bytes
#define ASSOOFS_SUPERBLOCK_BLOCK_NUMBER 0       // The superblock block
#define ASSOOFS_INODESTORE_BLOCK_NUMBER 1       // The inode store block
#define ASSOOFS_ROOTDIR_BLOCK_NUMBER    2       // The root directory block

#define ASSOOFS_ROOTDIR_INODE_NUMBER    1       // The inode number of the root directory

#define ASSOOFS_FILESYSTEM_MAX_OBJECTS  64      // The max number of inodes
#define ASSOOFS_FILENAME_MAX_LENGTH     255     // The max number of characters per filename

#define ASSOOFS_LAST_RESERVED_BLOCK ASSOOFS_ROOTDIR_BLOCK_NUMBER    // The last reserved block number
#define ASSOOFS_LAST_RESERVED_INODE ASSOOFS_ROOTDIR_INODE_NUMBER    // The last reserved inode number

/**
 * The superblock structure
 */
struct assoofs_super_block {
	uint64_t magic;         // The magic number field
	uint64_t version;       // The version field
	uint64_t block_size;    // The block size field
	uint64_t inodes_count;  // The number of inodes
	uint64_t free_blocks;   // The free status of all blocks (bit 1 for free, bit 0 for occupied)

	char padding[4056];     // Some padding space (4056 bytes)
};

/**
 * The directory structure
 */
struct assoofs_dir_record_entry {
	char filename[ASSOOFS_FILENAME_MAX_LENGTH]; // The filename
	uint64_t inode_no;                          // The inode number
};

/**
 * The inode structure
 */
struct assoofs_inode {
	// TODO: ADD TIME FIELD
	mode_t mode;                // The kind of inode (directory, file...)
	uint64_t inode_no;          // The corresponding inode
	uint64_t data_block_number; // The corresponding data block

	union {
		uint64_t file_size;             // The size of the file in bytes
		uint64_t dir_children_count;    // The number of files in a directory
	};
};