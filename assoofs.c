/**
 * Include dependencies
 */
#include <linux/module.h>       // Needed by all modules
#include <linux/kernel.h>       // Needed for KERN_INFO
#include <linux/init.h>         // Needed for the macros
#include <linux/fs.h>           // Needed for libfs
#include <linux/buffer_head.h>  // Needed for buffer_head
#include <linux/slab.h>         // Needed for kmem_cache

#include "assoofs.h"


/**
 * Some macros for easy reading
 */
#define info(fmt)                       printk(KERN_INFO ASSOOFS_NAME ": " fmt)                     // Print an information message
#define info1(fmt, arg1)                printk(KERN_INFO ASSOOFS_NAME ": " fmt, arg1)               // Print an information message with 1 argument
#define info2(fmt, arg1, arg2)          printk(KERN_INFO ASSOOFS_NAME ": " fmt, arg1, arg2)         // Print an information message with 2 arguments
#define info3(fmt, arg1, arg2, arg3)    printk(KERN_INFO ASSOOFS_NAME ": " fmt, arg1, arg2, arg3)   // Print an information message with 3 arguments

#define error(fmt)                      printk(KERN_ERR ASSOOFS_NAME ": " fmt)                      // Print an error message
#define error1(fmt, arg1)               printk(KERN_ERR ASSOOFS_NAME ": " fmt, arg1)                // Print an error message with 1 argument
#define error2(fmt, arg1, arg2)         printk(KERN_ERR ASSOOFS_NAME ": " fmt, arg1, arg2)          // Print an error message with 2 arguments
#define error3(fmt, arg1, arg2, arg3)   printk(KERN_ERR ASSOOFS_NAME ": " fmt, arg1, arg2, arg3)    // Print an error message with 3 arguments


/**
 * Function declarations (definitions are in this same order)
 */
static int __init assoofs_init(void);
static void __exit assoofs_exit(void);

static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
int assoofs_fill_super(struct super_block *sb, void *data, int silent);
static void assoofs_kill_block_super(struct super_block *sb);

int assoofs_delete_inode(struct inode *inode/*, struct dentry * dentry*/);

static int assoofs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static int assoofs_mkdir(struct user_namespace *mnt_userns, struct inode *dir , struct dentry *dentry, umode_t mode);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);

static int assoofs_iterate(struct file *file, struct dir_context *ctx);

ssize_t assoofs_read(struct file * file, char __user * buf, size_t len, loff_t * pos);
ssize_t assoofs_write(struct file * file, const char __user * buf, size_t len, loff_t * pos);

void *read_block(struct super_block *sb, struct buffer_head **bh, uint64_t number);
int assoofs_save_inode(struct super_block *sb, struct assoofs_inode *assoofs_inode);
struct assoofs_inode *assoofs_get_inode(struct super_block *sb, uint64_t inode_num);


/**
 * Some static structures
 */
// Some filesystem metadata
static struct file_system_type assoofs_type = {
	.owner = THIS_MODULE,
	.name = ASSOOFS_NAME,
	.mount = assoofs_mount,
	.kill_sb = assoofs_kill_block_super,
};

// Operations supported on the superblock
static struct super_operations assoofs_sb_ops = {
	.drop_inode = assoofs_delete_inode,
};

// Operations supported on inodes
static struct inode_operations assoofs_inode_ops = {
	.create = assoofs_create,
	.mkdir = assoofs_mkdir,
	.lookup = assoofs_lookup,
	//.rmdir = assoofs_delete_inode,
};

// Operations supported on directories
static struct file_operations assoofs_dir_ops = {
	.owner = THIS_MODULE,
	.iterate = assoofs_iterate,
};

// Operations supported on regular files
static struct file_operations assoofs_file_ops = {
	.read = assoofs_read,
	.write = assoofs_write,
};

// Superblock mutex
static DEFINE_MUTEX(assoofs_super_lock);
// Inode store mutex
static DEFINE_MUTEX(assoofs_inode_lock);



/**
 * Register the filesystem and check for errors
 */
static int __init assoofs_init(void) {

	int code;
	
	info("Registering filesystem\n");
	
	// use the libfs function
	code = register_filesystem(&assoofs_type);

	// print the correct message
	if (code)
		error1("Error during filesystem register. Code=%d\n", code);
	else
		info("Filesystem successfully registered\n");

	// return the code
	return code;
}

/**
 * Unregister the filesystem and check for errors
 */
static void __exit assoofs_exit(void) {

	int code;

	info("Unregistering filesystem\n");
	
	// use the libfs function
	code = unregister_filesystem(&assoofs_type);

	// print the correct message
	if (code)
		error1("Error during filesystem unregister. Code=%d\n", code);
	else
		info("Successfully unregistered\n");
}


/**
 * Mount an assoofs device
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

	struct dentry *entry;

	info("Mounting filesystem\n");

	// mount the device using assoofs_fill_super() to populate the superblock
	entry = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);

	if (IS_ERR(entry))
		error("Error during mounting\n");
	else
		info1("Successfully mounted on '%s'\n", dev_name);

	// return the directory entry
	return entry;
}

/**
 * Populate the superblock for device mount
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {

	struct buffer_head *bh;
	struct assoofs_super_block *sb_disk;
	struct inode *root_inode;
	struct dentry *root_dentry;

	info("Reading superblock\n");

	// get the superblock from disk
	sb_disk = (struct assoofs_super_block *) read_block(sb, &bh, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
	
	// return -1 if the block hasn't been read
	if (!sb_disk) return -1;

	// print superblock info
	info3("Superblock read: magic=%llu, version=%llu, block_size=%llu\n", sb_disk->magic, sb_disk->version, sb_disk->block_size);

	// check some data from the superblock
	if (sb_disk->magic != ASSOOFS_MAGIC) {

		error1("Magic number mismatch (expected '%d'). Refusing to mount\n", ASSOOFS_MAGIC);
		brelse(bh);
		return -2;
	}
	if (sb_disk->version != ASSOOFS_VERSION) {

		error1("Version mismatch (expected '%d'). Refusing to mount\n", ASSOOFS_VERSION);
		brelse(bh);
		return -3;
	}
	if (sb_disk->block_size != ASSOOFS_BLOCK_SIZE) {

		error1("Block size mismatch (expected '%d'). Refusing to mount\n", ASSOOFS_BLOCK_SIZE);
		brelse(bh);
		return -4;
	}

	// store the data on memory
	sb->s_magic = ASSOOFS_MAGIC;
	sb->s_maxbytes = ASSOOFS_BLOCK_SIZE;

	sb->s_fs_info = sb_disk;
	sb->s_op = &assoofs_sb_ops;

	// create the root inode
	root_inode = new_inode(sb);
	if (!root_inode) {

		error("Error creating inode. Aborting mount\n");
		brelse(bh);
		return -5;
	}
	
	// initialize the root inode
	inode_init_owner(sb->s_user_ns, root_inode, NULL, S_IFDIR);
	root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
	root_inode->i_sb = sb;
	root_inode->i_op = &assoofs_inode_ops;
	root_inode->i_fop = &assoofs_dir_ops;

	root_inode->i_private = assoofs_get_inode(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);

	root_inode->i_atime = ((struct assoofs_inode *)root_inode->i_private)->time;
	root_inode->i_mtime = ((struct assoofs_inode *)root_inode->i_private)->time;
	root_inode->i_ctime = ((struct assoofs_inode *)root_inode->i_private)->time;

	// add the root inode to the superblock, checking it
	root_dentry = d_make_root(root_inode);
	if (!root_dentry) {

		error("Error creating root directory");
		brelse(bh);
		return -5;
	}

	sb->s_root = root_dentry;

	// release resources and return normally
	brelse(bh);
	return 0;
}

/**
 * A wrapper for the device unmount
 */
static void assoofs_kill_block_super(struct super_block *sb) {
	
	info("Destroying superblock\n");

	// use the libfs function
	kill_block_super(sb);

	info("Superblock destroyed. Filesystem unmounted\n");
}


/**
 * A wrapper for inode deletion
 */
// TODO: implement something that DOES WORK
int assoofs_delete_inode(struct inode *inode/*, struct dentry * dentry*/) {/*

	// get the superblock (linux, assoofs and disk)
	struct super_block *sb = inode->i_sb;
	struct assoofs_super_block *assoofs_sb = (struct assoofs_super_block *) sb->s_fs_info;
	struct assoofs_super_block *disk_sb;

	// declare the variables
	struct buffer_head *bh;
	struct assoofs_inode *assoofs_inode = inode->i_private;
	struct assoofs_inode *inode_iterator;

	uint64_t i;
	uint64_t one = 1;

	info1("Deleting inode %llu\n", assoofs_inode->inode_no);

	// update the mask to the correct free block
	one <<= assoofs_inode->data_block_number;

	//if (mutex_lock_interruptible(&assoofs_sb_lock)) {
	//	sfs_trace("Failed to acquire mutex lock\n");
	//	return -EINTR;
	//}


	// get the inode store
	inode_iterator = (struct assoofs_inode *) read_block(sb, &bh, ASSOOFS_INODESTORE_BLOCK_NUMBER);

	// iterate over the whole inode store
	for (i = 0; i < (ASSOOFS_FILESYSTEM_MAX_OBJECTS - 1); i++) {

		// check if the delete inode is the same as the inode_iterator
		if (assoofs_inode->inode_no == inode_iterator->inode_no) {

			// set the delete inode as the current of the iterator and move the iterator
			assoofs_inode = inode_iterator;
			inode_iterator++;

			// copy the contents of the iterator to the delete inode (previous position)
			memcpy(assoofs_inode, inode_iterator, sizeof(*inode_iterator));

			// move the delete inode one position
			assoofs_inode++;

		} else {

			// move the iterator one position
			inode_iterator++;
		}

	}

	// save changes to disk
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	// get the superblock from disk
	disk_sb = (struct assoofs_super_block *) read_block(sb, &bh, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);

	// update the inodes count and free blocks
	assoofs_sb->free_blocks |= one;
	disk_sb->free_blocks |= one;

	assoofs_sb->inodes_count--;
	disk_sb->inodes_count--;

	// save changes to disk
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	info1("Inode %llu deleted\n", assoofs_inode->inode_no);

	//mutex_unlock(&assoofs_sb_lock);

	return 0;
	*/


	int code;

	info("Deleting inode\n");
	
	// use the libfs function (DOES ABSOLUTELY NOTHING)
	code = generic_delete_inode(inode);

	if (code)
		info1("Failed to delete inode. code=%d\n", code);
	else
		info("Inode deleted correctly\n");

	return code;

}


/**
 * Create a file
 */
static int assoofs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {

	// get the superblock (linux and assoofs)
	struct super_block *sb = dir->i_sb;
	struct assoofs_super_block *assoofs_sb = (struct assoofs_super_block *) sb->s_fs_info;

	// declare other variables
	struct buffer_head *bh;
	struct inode *inode;
	struct assoofs_inode *assoofs_inode;
	struct assoofs_inode *inode_iterator;

	struct assoofs_inode *parent_dir_inode = dir->i_private;
	struct assoofs_dir_record_entry *dir_record_iterator;

	uint64_t count;
	uint64_t i;
	uint64_t one = 1;

	info("Creating file/folder\n");

	if (mutex_lock_interruptible(&assoofs_super_lock)) {

		error("Failed to acquire superblock mutex\n");
		return -1;
	}

	// get the number of inodes
	count = assoofs_sb->inodes_count;

	// verify it can be created
	if (count >= ASSOOFS_FILESYSTEM_MAX_OBJECTS) {

		error("Cant create file/folder: Reached maximum number of objects supported\n");		
		mutex_unlock(&assoofs_super_lock);
		return -2;
	}

	// verify it is a file or a folder
	if (!S_ISDIR(mode) && !S_ISREG(mode)) {
		
		error("Cant create file/folder: Trying to create an unrecognized inode type\n");
		mutex_unlock(&assoofs_super_lock);
		return -3;
	}

	// create the linux inode and populate it
	inode = new_inode(sb);
	if (!inode) {

		error("Cant create file/folder: Error creating inode.\n");
		mutex_unlock(&assoofs_super_lock);
		return -4;
	}

	inode->i_sb = sb;
	inode->i_op = &assoofs_inode_ops;
	inode->i_ino = count + 1;


	// create the assoofs inode and initialize it
	assoofs_inode = kmalloc(sizeof(struct assoofs_inode), GFP_KERNEL);
	assoofs_inode->inode_no = inode->i_ino;
	assoofs_inode->mode = mode;
	
	assoofs_inode->time = current_time(inode);
	inode->i_atime = assoofs_inode->time;
	inode->i_mtime = assoofs_inode->time;
	inode->i_ctime = assoofs_inode->time;

	inode->i_private = assoofs_inode;

	// distinguish between files and directories
	if (S_ISREG(mode)) {
		
		info("Populating file inode\n");

		assoofs_inode->file_size = 0;
		inode->i_fop = &assoofs_file_ops;

	} else if (S_ISDIR(mode)) {

		info("Populating folder inode\n");

		assoofs_inode->dir_children_count = 0;
		inode->i_fop = &assoofs_dir_ops;
	}

	// find a free block
	info("Getting free block for file\n");
	for (i = ASSOOFS_LAST_RESERVED_BLOCK + 1; i < ASSOOFS_FILESYSTEM_MAX_OBJECTS; i++) {

		// NOTE: the kernel warns about undefined behaviour if the shift is performed on int (32 bits) (by default on all numeric variables)
		if (assoofs_sb->free_blocks & (one << i)) 
			break;
	}

	// exit if a free block cant be found
	if (i >= ASSOOFS_FILESYSTEM_MAX_OBJECTS) {

		error("Cant create file/folder: No more free blocks available\n");
		mutex_unlock(&assoofs_super_lock);
		return -5;
	}

	// save the free block found and remove it from the list
	assoofs_inode->data_block_number = i;
	assoofs_sb->free_blocks &= ~(one << i);

	// get the superblock buffer head
	if (!read_block(sb, &bh, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER)) {

		mutex_unlock(&assoofs_super_lock);
		return -6;
	}

	// update the data of the superblock and sync it with disk
	bh->b_data = (char *) assoofs_sb;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	info1("Saving inode %llu to disk\n", assoofs_inode->inode_no);

	if (mutex_lock_interruptible(&assoofs_inode_lock)) {

		error("Failed to acquire inode store mutex\n");
		mutex_unlock(&assoofs_super_lock);
		return -7;
	}


	// get the inode store pointer
	inode_iterator = (struct assoofs_inode *) read_block(sb, &bh, ASSOOFS_INODESTORE_BLOCK_NUMBER);

	if (!inode_iterator) {

		mutex_unlock(&assoofs_super_lock);
		mutex_unlock(&assoofs_inode_lock);
		return -8;
	}
	
	// move to the end of the list
	inode_iterator += assoofs_sb->inodes_count;

	// copy the inode at the end of the list (append)
	memcpy(inode_iterator, assoofs_inode, sizeof(struct assoofs_inode));
	assoofs_sb->inodes_count++;

	// write the inode store to disk
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	// get the superblock buffer head
	if (!read_block(sb, &bh, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER)) {

		mutex_unlock(&assoofs_super_lock);
		mutex_unlock(&assoofs_inode_lock);
		return -9;
	}

	// update the data of the superblock and sync it with disk
	bh->b_data = (char *) assoofs_sb;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);


	// get the parent directory data block
	dir_record_iterator = (struct assoofs_dir_record_entry *) read_block(sb, &bh, parent_dir_inode->data_block_number);

	if (!dir_record_iterator) {
		
		mutex_unlock(&assoofs_super_lock);
		mutex_unlock(&assoofs_inode_lock);
		return -10;
	}

	// navigate to the last record in the parent directory
	dir_record_iterator += parent_dir_inode->dir_children_count;

	// set the inode number and the filename
	dir_record_iterator->inode_no = assoofs_inode->inode_no;
	strcpy(dir_record_iterator->filename, dentry->d_name.name);

	// write the changes to disk
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);


	// update the number of files in directory and save the changes, handling any errors
	parent_dir_inode->dir_children_count++;
	if (assoofs_save_inode(sb, parent_dir_inode)) {

		error("Cant create file/folder: Error updating parent folder data\n");

		mutex_unlock(&assoofs_super_lock);
		mutex_unlock(&assoofs_inode_lock);
		return -11;
	}

	mutex_unlock(&assoofs_super_lock);
	mutex_unlock(&assoofs_inode_lock);

	// initialize the owner of the inode, add it to the directory and exit normally
	inode_init_owner(sb->s_user_ns, inode, dir, mode);
	d_add(dentry, inode);

	return 0;
}

/**
 * Create a directory
 */
static int assoofs_mkdir(struct user_namespace *mnt_userns, struct inode *dir , struct dentry *dentry, umode_t mode) {

	// just use the create function for the job, but changing it to a directory
	return assoofs_create(mnt_userns, dir, dentry, S_IFDIR | mode, false);
}

/*
 * Find a children file inside a folder
 */
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {

	// get the superblock and the parent inode
	struct super_block *sb = parent_inode->i_sb;
	struct assoofs_inode *parent = parent_inode->i_private;

	// declare some variables
	int i;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *record;
	struct inode *inode;
	struct assoofs_inode *assoofs_inode;

	info3("Looking up file '%s' inside inode %llu (data block %llu)\n", child_dentry->d_name.name, parent->inode_no, parent->data_block_number);

	// get the parent directory record from disk
	record = (struct assoofs_dir_record_entry *) read_block(sb, &bh, parent->data_block_number);
	
	// return NULL if the block hasn't been read
	if (!record) return NULL;

	// iterate over all the files in the directory
	for (i = 0; i < parent->dir_children_count; i++) {

		// check if the file is the requested
		if (!strcmp(record->filename, child_dentry->d_name.name)) {

			info3("File '%s' (inode %llu) found in inode %llu\n", child_dentry->d_name.name, record->inode_no, parent->inode_no);
			
			// get the assoofs inode and create the linux one
			if (mutex_lock_interruptible(&assoofs_super_lock)) {

				error("Failed to acquire superblock mutex\n");
				brelse(bh);
				return NULL;
			}
			if (mutex_lock_interruptible(&assoofs_inode_lock)) {

				error("Failed to acquire inode store mutex\n");
				mutex_unlock(&assoofs_super_lock);
				brelse(bh);
				return NULL;
			}

			assoofs_inode = assoofs_get_inode(sb, record->inode_no);
			
			mutex_unlock(&assoofs_super_lock);
			mutex_unlock(&assoofs_inode_lock);

			inode = new_inode(sb);
			if (!inode) {

				error("Error on lookup: cant create inode\n");
				brelse(bh);
				return NULL;
			}

			// initialize the linux inode
			inode->i_ino = record->inode_no;
			inode->i_sb = sb;
			inode->i_op = &assoofs_inode_ops;
			inode->i_private = assoofs_inode;

			inode->i_atime = assoofs_inode->time;
			inode->i_mtime = assoofs_inode->time;
			inode->i_ctime = assoofs_inode->time;

			// use the correct type (directory or file)
			if (S_ISDIR(assoofs_inode->mode))
				inode->i_fop = &assoofs_dir_ops;
			else if (S_ISREG(assoofs_inode->mode))
				inode->i_fop = &assoofs_file_ops;
			else
				error("Error on lookup: unknown inode type.\n");

			// initialize the owner of the inode
			inode_init_owner(sb->s_user_ns, inode, parent_inode, assoofs_inode->mode);
			
			// add it to the child entry
			d_add(child_dentry, inode);
			
			// release resources and exit
			brelse(bh);
			return NULL;
		}

		// prepare the next inode to search
		record++;
	}

	// print an error message if its not found and return
	error2("Filename '%s' not found in inode %llu\n", child_dentry->d_name.name, parent->inode_no);
	brelse(bh);
	return NULL;
}


/*
 * Read a whole directory
 */
// TODO: USE FILE MUTEXES FOR CONCURRENT READ-WRITE? (they are struct mutex as well)
static int assoofs_iterate(struct file *file, struct dir_context *ctx) {

	// declare and get the inodes (linux and assoofs) the superblock
	struct inode *inode = file->f_path.dentry->d_inode;
	struct assoofs_inode *assoofs_inode = inode->i_private;
	struct super_block *sb = inode->i_sb;

	// declare the rest of the variables
	int i;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *record;

	info1("Reading directory '%s' contents\n", file->f_path.dentry->d_name.name);

	// check if the context is already created, exiting if it is not
	if (ctx->pos) {

		error("Directory context is not initialized\n");
		return -1;
	}

	// check if the inode is a directory, exiting if its not
	if (!S_ISDIR(assoofs_inode->mode)) {
		
		error3("Inode (%llu, %lu) for file '%s' is not a directory\n", assoofs_inode->inode_no, inode->i_ino, file->f_path.dentry->d_name.name);
		return -2;
	}

	// read the directory record from disk
	record = (struct assoofs_dir_record_entry *) read_block(sb, &bh, assoofs_inode->data_block_number);

	if (!record) return -3;

	// iterate over all files in directory
	for (i = 0; i < assoofs_inode->dir_children_count; i++) {

		// add the file to the context
		dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAX_LENGTH, record->inode_no, DT_UNKNOWN);
		ctx->pos += sizeof(struct assoofs_dir_record_entry);
		
		// prepare the next record
		record++;
	}

	info2("Directory '%s' read. Found %d inodes\n", file->f_path.dentry->d_name.name, i);

	// release resources and return
	brelse(bh);
	return 0;
}


/*
 * Read from a file
 */
// TODO: USE FILE MUTEXES FOR CONCURRENT READ-WRITE? (they are struct mutex as well)
ssize_t assoofs_read(struct file * file, char __user * buf, size_t len, loff_t * pos) {

	// declare and get the assoofs inode and the superblock
	struct assoofs_inode *inode = file->f_path.dentry->d_inode->i_private;
	struct super_block *sb = file->f_path.dentry->d_inode->i_sb;

	// declare other variables
	struct buffer_head *bh;
	char *buffer;

	size_t left;
	int nbytes;

	info3("Trying to read %lu bytes from file '%s', starting from byte %llu\n", len, file->f_path.dentry->d_name.name, *pos);

	// prevent reading data outside the file
	if (*pos >= inode->file_size) {
	
		error("Cant read from disk: Trying to read outside the file\n");
		return 0;
	}

	// get the file block
	buffer = (char *) read_block(sb, &bh, inode->data_block_number);
	
	// return 0 if the block hasn't been read
	if (!buffer) return 0;

	// move the buffer to the current position
	buffer += *pos;

	// find the amount of bytes to read
	left = (size_t) inode->file_size - (size_t) *pos;
	nbytes = min(left, len);

	// copy the data from the block buffer to the userspace buffer, checking for errors
	if (copy_to_user(buf, buffer, nbytes)) {

		error("Error copying file contents to the userspace buffer\n");
		
		// release resources and return
		brelse(bh);
		return 0;
	}

	// update the current position
	*pos += nbytes;

	info2("Read %d bytes from file '%s'\n", nbytes, file->f_path.dentry->d_name.name);

	// release resources and return the amount of bytes read
	brelse(bh);
	return nbytes;
}

/*
 * Write to a file
 */
// TODO: USE FILE MUTEXES FOR CONCURRENT READ-WRITE? (they are struct mutex as well)
ssize_t assoofs_write(struct file * file, const char __user * buf, size_t len, loff_t * pos) {

	// declare and get the assoofs inode and the superblock
	struct assoofs_inode *inode = file->f_path.dentry->d_inode->i_private;
	struct super_block *sb = file->f_path.dentry->d_inode->i_sb;

	// declare some variables
	struct buffer_head *bh;
	char *buffer;

	info3("Trying to write %lu bytes from file '%s', starting from byte %llu\n", len, file->f_path.dentry->d_name.name, *pos);
	
	// verify the amount of space to write
	if (*pos + len >= ASSOOFS_BLOCK_SIZE) {

		error("Cant write to disk: File size after write exceeds block size\n");
		return 0;
	}

	// get the file block
	buffer = (char *) read_block(sb, &bh, inode->data_block_number);
	
	// return 0 if the block hasn't been read
	if (!buffer) return 0;

	// move the buffer to the current position
	buffer += *pos;    

	// copy the buffer to the userspace buffer, checking for errors
	if (copy_from_user(buffer, buf, len)) {

		error("Error copying userspace buffer to the file buffer\n");
		
		// release resources and return
		brelse(bh);
		return 0;
	}

	// update the current position
	*pos += len;

	// write changes to disk
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);

	// update the inode information
	inode->file_size = *pos;

	if (mutex_lock_interruptible(&assoofs_super_lock)) {

		error("Failed to acquire superblock mutex\n");
		brelse(bh);
		return 0;
	}
	if (mutex_lock_interruptible(&assoofs_inode_lock)) {

		error("Failed to acquire inode store mutex\n");
		mutex_unlock(&assoofs_super_lock);
		brelse(bh);
		return 0;
	}

	assoofs_save_inode(sb, inode);

	mutex_unlock(&assoofs_super_lock);
	mutex_unlock(&assoofs_inode_lock);

	info2("Written %lu bytes to file '%s'\n", len, file->f_path.dentry->d_name.name);

	// release resources and return the amount of bytes read
	brelse(bh);
	return len;
}


/**
 * Read a block data from disk printing a message if it can't be read
 * NOTE: on successful read, it is necessary to release the buffer head after use
 */
void *read_block(struct super_block *sb, struct buffer_head **bh, uint64_t number) {

	// read the buffer head and store it
	struct buffer_head *tmp;
	tmp = sb_bread(sb, number);

	// verify the buffer head
	if (!tmp) {

		// print a message and release the buffer head
		error1("Error reading block %llu\n", number);
		brelse(tmp);

		// return NULL to signal the error
		return NULL;
	}

	// save the buffer head
	*bh = tmp;

	// return the requested data
	return tmp->b_data;
}

/**
 * Update an inode on disk
 */
int assoofs_save_inode(struct super_block *sb, struct assoofs_inode *assoofs_inode) {

	// declare the variables
	struct buffer_head *bh;
	struct assoofs_inode *inode_iterator;
	uint64_t count = 0;
	uint64_t inode_num;

	info1("Updating inode %llu\n", assoofs_inode->inode_no);

	// get the number of inodes
	inode_num = ((struct assoofs_super_block *) sb->s_fs_info)->inodes_count;

	// get the inode store
	inode_iterator = (struct assoofs_inode *) read_block(sb, &bh, ASSOOFS_INODESTORE_BLOCK_NUMBER);

	if (!inode_iterator) return -1;

	// search for the inode to update
	info2("Searching inode %llu, starting from inode %llu\n", assoofs_inode->inode_no, inode_iterator->inode_no);

	// iterate until its found or there are no more inodes
	while ((inode_iterator->inode_no != assoofs_inode->inode_no) && (count < inode_num)) {
		
		count++;

		// move to the next inode
		inode_iterator++;
	}

	// exit if the inode is not found
	if (inode_iterator->inode_no != assoofs_inode->inode_no) {

		error1("Cant update inode to disk: Inode %llu not found\n", assoofs_inode->inode_no);
		brelse(bh);
		return -2;
	}

	info1("Inode %llu found\n", assoofs_inode->inode_no);

	// store the inode and save to disk
	memcpy(inode_iterator, assoofs_inode, sizeof(*inode_iterator));
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);

	info1("Inode %llu updated\n", assoofs_inode->inode_no);

	brelse(bh);
	return 0;
}

/**
 * Get an inode from the store with the specified number (if exists)
 */
struct assoofs_inode *assoofs_get_inode(struct super_block *sb, uint64_t inode_num) {

	// declare get the superblock
	struct assoofs_super_block *assoofs_sb = sb->s_fs_info;

	// prepare a default value to return
	struct assoofs_inode *inode_buffer = NULL;

	// declare some variables
	struct buffer_head *bh;
	struct assoofs_inode *assoofs_inode;
	int i;

	info1("Getting inode number %llu\n", inode_num);

	// get the inode block
	assoofs_inode = (struct assoofs_inode *) read_block(sb, &bh, ASSOOFS_INODESTORE_BLOCK_NUMBER);

	// return the default value if the block hasn't been read
	if (!assoofs_inode) return inode_buffer;

	// iterate over all inodes until the requested inode is found
	for (i = 0; i < assoofs_sb->inodes_count; i++) {

		// when found, copy it to the buffer, and exit the loop
		if (assoofs_inode->inode_no == inode_num) {

			info1("Inode %llu found\n", inode_num);

			inode_buffer = kmalloc(sizeof(struct assoofs_inode), GFP_KERNEL);
			memcpy(inode_buffer, assoofs_inode, sizeof(*inode_buffer));

			break;
		}

		// change to the next inode
		assoofs_inode++;
	}


	if (inode_buffer == NULL) 
		error1("Inode %llu not found\n", inode_num);

	// release resources and return the requested inode
	brelse(bh);
	return inode_buffer;
}



/**
 * Register the load and unload functions
 */
module_init(assoofs_init);
module_exit(assoofs_exit);

/**
 * Register some module metadata
 */
MODULE_AUTHOR("msahes00");
MODULE_LICENSE("GPL");