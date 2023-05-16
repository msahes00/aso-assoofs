#include "assoofs.h"

/**
 * Read a block data from disk printing a message if it can't be read
 * NOTE: on successful read, it is necessary to release the buffer head after use
 */
void *read_block(struct super_block *sb, struct buffer_head **bh, uint64_t number) {

    // read the buffer head and store it
    struct buffer_head *tmp = sb_bread(sb, number);

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
    return (*bh)->b_data;
}

/*
 * Read from a file
 */
ssize_t assoofs_read(struct file * file, char __user * buf, size_t len, loff_t * pos) {

    // get the assoofs inode and the superblock
    struct assoofs_inode *inode = file->f_path.dentry->d_inode->i_private;
    struct super_block *sb = file->f_path.dentry->d_inode->i_sb;
	struct buffer_head *bh;

    // prevent reading data outside the file
	if (*pos >= inode->file_size) return 0;

    // get the file block
    char *buffer = (char *) read_block(sb, &bh, inode->data_block_number);
    
    // return 0 if the block hasn't been read
    if (!buffer) return 0;

    // move the buffer to the current position
    buffer += *pos;

    // find the amount of bytes to read
    size_t left = (size_t) inode->file_size - (size_t) *pos;
	int nbytes = min(left, len);

    // copy the buffer to the userspace buffer, checking for errors
	if (copy_to_user(buf, buffer, nbytes)) {

		error("Error copying file contents to the userspace buffer\n");
		
        // release resources and return
        brelse(bh);
		return -1;
	}

    // update the current position
	*pos += nbytes;

    // release resources and return the amount of bytes read
	brelse(bh);
	return nbytes;
}

/*
 * Write to a file
 */
ssize_t assoofs_write(struct file * file, const char __user * buf, size_t len, loff_t * pos) {
	
    // get the assoofs inode and the superblock
	struct assoofs_inode *inode = file->f_path.dentry->d_inode->i_private;
	struct super_block *sb = file->f_path.dentry->d_inode->i_sb;
	
    
    // verify the amount of space to write
    if (*pos + len >= ASSOOFS_BLOCK_SIZE) {

        error("Can`t write to disk: File size after write exceeds block size\n");
        return -1;
    }

    // get the file block
	struct buffer_head *bh;
    char *buffer = (char *) read_block(sb, &bh, inode->data_block_number);
    
    // return 0 if the block hasn't been read
    if (!buffer) return 0;

    // move the buffer to the current position
    buffer += *pos;    

    // copy the buffer to the userspace buffer, checking for errors
	if (copy_from_user(buffer, buf, len)) {

		error("Error copying userspace buffer to the file buffer\n");
		
        // release resources and return
        brelse(bh);
		return -2;
	}

    // update the current position
	*pos += len;

    // write changes to disk
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    // update the inode information
    inode->file_size = *pos;
    assoofs_save_inode(sb, inode);

    // release resources and return the amount of bytes read
	brelse(bh);
	return len;
}

/*
 * Read a whole directory
 */
static int assoofs_iterate(struct file *file, struct dir_context *ctx) {

    info("Iterating over directory\n");

    // get the inode and the superblock
	struct inode *inode = file->f_path.dentry->d_inode;
	struct super_block *sb = inode->i_sb;

    // check if the context is already created, exiting if it is not
	if (ctx->pos) return 0;

    // get the assoofs inode
	struct assoofs_inode *assoofs_inode = inode->i_private;

    // check if the inode is a directory, exiting if its not
	if (!S_ISDIR(assoofs_inode->mode)) {
		
        error3("Inode (%llu, %lu) for file '%s' is not a directory\n", assoofs_inode->inode_no, inode->i_ino, file->f_path.dentry->d_name.name);
		return -1;
	}

    // read the directory record
	struct buffer_head *bh;
    struct assoofs_dir_record_entry *record = (struct assoofs_dir_record_entry *) read_block(sb, &bh, assoofs_inode->data_block_number);

    // iterate over all files in directory
	for (int i = 0; i < assoofs_inode->dir_children_count; i++) {

        // add the file to the context
		dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAX_LENGTH, record->inode_no, DT_UNKNOWN);
		ctx->pos += sizeof(struct assoofs_dir_record_entry);
		
        // prepare the next record
        record++;
	}

    // release resources and return
	brelse(bh);
	return 0;
}

/**
 * Convert an assoofs inode to an standard inode
 */
static struct inode *assoofs_convert_inode(struct super_block *sb, struct assoofs_inode *assoofs_inode, int inode_num) {
	
    // create the standard inode
    struct inode *inode = new_inode(sb);
	
    // initialize it
    inode->i_ino = inode_num;
	inode->i_sb = sb;
	inode->i_op = &assoofs_inode_ops;
	inode->i_private = assoofs_inode;

	struct timespec64 time_now = current_time(inode);
    inode->i_atime = time_now;
    inode->i_mtime = time_now;
    inode->i_ctime = time_now;

    // use the correct type (directory or file)
	if (S_ISDIR(assoofs_inode->mode))
		inode->i_fop = &assoofs_dir_ops;
	else if (S_ISREG(assoofs_inode->mode))
		inode->i_fop = &assoofs_file_ops;
	else
		error("Error converting inode: unknown inode type.\n");

	return inode;
}


/*
 * Find a children file inside a folder
 */
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {

    // get the superblock and the parent inode
    struct super_block *sb = parent_inode->i_sb;
    struct assoofs_inode *parent = parent_inode->i_private;

    info2("Looking up inode %llu and block %llu\n", parent->inode_no, parent->data_block_number);

    // get the parent directory record block
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record = (struct assoofs_dir_record_entry *) read_block(sb, &bh, parent->data_block_number);
    
    // return NULL if the block hasn't been read
    if (!record) return NULL;

    // iterate over all the files in the directory
    for (int i = 0; i < parent->dir_children_count; i++) {
        
        info2("Lookup file '%s' (inode %llu)\n", record->filename, record->inode_no);

        // check if the file is the requested
        if (!strcmp(record->filename, child_dentry->d_name.name)) {
            
            // get the true inode for the file
            struct inode *inode = assoofs_convert_inode(sb, assoofs_get_inode(sb, record->inode_no), record->inode_no);

            // initialize the inode
            struct assoofs_inode *tmp_inode = (struct assoofs_inode *) inode->i_private;
            inode_init_owner(sb->s_user_ns, inode, parent_inode, tmp_inode->mode);
            
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
    error1("No inode found for the filename '%s'\n", child_dentry->d_name.name);
    brelse(bh);
    return NULL;
}


static int assoofs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");
    // TODO
    return 0;
}

static int assoofs_mkdir(struct user_namespace *mnt_userns, struct inode *dir , struct dentry *dentry, umode_t mode) {
    printk(KERN_INFO "New directory request\n");
    // TODO
    return 0;
}

/**
 * A wrapper for inode deletion
 */
int assoofs_delete_inode(struct inode *inode) {

    info("Deleting inode\n");
    
    // use the libfs function
    int code = generic_delete_inode(inode);

    info1("Inode deleted: code=%d\n", code);

    return code;
}

int assoofs_save_inode(struct super_block *sb, struct assoofs_inode *assoofs_inode) {

    // TODO: complete
	struct simplefs_inode *inode_iterator;
	struct buffer_head *bh;

	bh = sb_bread(sb, SIMPLEFS_INODESTORE_BLOCK_NUMBER);
	BUG_ON(!bh);

	if (mutex_lock_interruptible(&simplefs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}

	inode_iterator = simplefs_inode_search(sb,
		(struct simplefs_inode *)bh->b_data,
		sfs_inode);

	if (likely(inode_iterator)) {
		memcpy(inode_iterator, sfs_inode, sizeof(*inode_iterator));
		printk(KERN_INFO "The inode updated\n");

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	} else {
		mutex_unlock(&simplefs_sb_lock);
		printk(KERN_ERR
		       "The new filesize could not be stored to the inode.");
		return -EIO;
	}

	brelse(bh);

	mutex_unlock(&simplefs_sb_lock);

	return 0;
}

/**
 * Get an inode from the store with the specified number (if exists)
 */
struct assoofs_inode *assoofs_get_inode(struct super_block *sb, uint64_t inode_num) {
    
    info1("Getting inode number %llu\n", inode_num);

    // prepare a default value to return
    struct assoofs_inode *inode_buffer = NULL;

    // get the inode block
    struct buffer_head *bh;
    struct assoofs_inode *assoofs_inode = (struct assoofs_inode *) read_block(sb, &bh, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    
    // return the default value if the block hasn't been read
    if (!assoofs_inode) return inode_buffer;

    // get the superblock
    struct assoofs_super_block *assoofs_sb = sb->s_fs_info;

/* TODO: mutex
    if (mutex_lock_interruptible(&assoofs_inodes_mgmt_lock)) {
        printk(KERN_ERR "Failed to acquire mutex lock %s +%d\n",
               __FILE__, __LINE__);
        return NULL;
    }
*/
    // iterate over all inodes until the requested inode is found
    for (int i = 0; i < assoofs_sb->inodes_count; i++) {

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

// TODO: mutex
//      mutex_unlock(&assoofs_inodes_mgmt_lock);

    // release resources and return the requested inode
    brelse(bh);
    return inode_buffer;
}

/**
 * Populate the superblock for device mount
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {

    info("Reading superblock\n");

    // get the superblock from disk
    struct buffer_head *bh;
    struct assoofs_super_block *sb_disk = (struct assoofs_super_block *) read_block(sb, &bh, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    
    // return -1 if the block hasn't been read
    if (!sb_disk) return -1;

    // print superblock info
    info3("Trying to mount assoofs: magic=%llu, version=%llu, block_size=%llu", sb_disk->magic, sb_disk->version, sb_disk->block_size);

    // check some data from the superblock
    if (sb_disk->magic != ASSOOFS_MAGIC) {

        error1("Magic number mismatch (expected '%d'). Refusing to mount", ASSOOFS_MAGIC);
        brelse(bh);
        return -2;
    }
    if (sb_disk->version != ASSOOFS_VERSION) {

        error1("Version mismatch (expected '%d'). Refusing to mount", ASSOOFS_VERSION);
        brelse(bh);
        return -3;
    }
    if (sb_disk->block_size != ASSOOFS_BLOCK_SIZE) {

        error1("Block size mismatch (expected '%d'). Refusing to mount", ASSOOFS_BLOCK_SIZE);
        brelse(bh);
        return -4;
    }

    // store the data on memory
    sb->s_magic = ASSOOFS_MAGIC;
    sb->s_maxbytes = ASSOOFS_BLOCK_SIZE;

    sb->s_fs_info = sb_disk;
    sb->s_op = &assoofs_sb_ops;

    // create the root inode
    struct inode *root_inode = new_inode(sb);
    
    // initialize the root inode
    inode_init_owner(sb->s_user_ns, root_inode, NULL, S_IFDIR);
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
    root_inode->i_sb = sb;
    root_inode->i_op = &assoofs_inode_ops;
    root_inode->i_fop = &assoofs_dir_ops;

    struct timespec64 time_now = current_time(root_inode);
    root_inode->i_atime = time_now;
    root_inode->i_mtime = time_now;
    root_inode->i_ctime = time_now;

    root_inode->i_private = assoofs_get_inode(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);

    // add the root inode to the superblock, checking it
    struct dentry *root = d_make_root(root_inode);
    if (!root) {

        error("Error creating root directory");
        brelse(bh);
        return -5;
    }

    sb->s_root = root;

    // release resources and return normally
    brelse(bh);
    return 0;
}


/**
 * Mount an assoofs device
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    info("Mounting filesystem\n");

    // mount the device using assoofs_fill_super() to populate the superblock
    struct dentry *entry = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);

    if (IS_ERR(entry))
        error("Error during mounting\n");
    else
        info1("Successfully mounted on '%s'\n", dev_name);

    // return the directory entry
    return entry;
}

/**
 * A wrapper for the device unmount
 */
static void assoofs_kill_block_super(struct super_block *sb) {
    
    info("Destroying superblock\n");

    // use the libfs function
    kill_block_super(sb);

    info("Superblock destroyed. Filesystem unmounted\n");
    return;
}


/**
 * Register the filesystem and check for errors
 */
static int __init assoofs_init(void) {
    
    info("Registering filesystem\n");
    
    // use the libfs function
    int code = register_filesystem(&assoofs_type);

    // print the correct message
    if (code)
        info("Filesystem successfully registered\n");
    else
        error1("Error during filesystem register. Code=%d\n", code);

    // return the code
    return code;
}

/**
 * Unregister the filesystem and check for errors
 */
static void __exit assoofs_exit(void) {

    info("Unregistering filesystem\n");
    
    // use the libfs function
    int code = unregister_filesystem(&assoofs_type);

    // print the correct message
    if (code)
        info("Successfully unregistered\n");
    else
        error1("Error during filesystem unregister. Code=%d\n", code);
}