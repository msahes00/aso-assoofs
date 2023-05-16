/**
 * Include dependencies
 */
#include <linux/module.h>      /* Needed by all modules */
#include <linux/kernel.h>      /* Needed for KERN_INFO  */
#include <linux/init.h>        /* Needed for the macros */
#include <linux/fs.h>          /* libfs stuff           */
#include <linux/buffer_head.h> /* buffer_head           */
#include <linux/slab.h>        /* kmem_cache            */

/**
 * Define some constants for the assoofs filesystem
 */
#define ASSOOFS_NAME "assoofs"
#define ASSOOFS_MAGIC 0x20200406
#define ASSOOFS_VERSION 1

#define ASSOOFS_BLOCK_SIZE 4096
#define ASSOOFS_SUPERBLOCK_BLOCK_NUMBER 0
#define ASSOOFS_INODESTORE_BLOCK_NUMBER 1
#define ASSOOFS_ROOTDIR_BLOCK_NUMBER 2

#define ASSOOFS_ROOTDIR_INODE_NUMBER 1

#define ASSOOFS_FILESYSTEM_MAX_OBJECTS 64
#define ASSOOFS_FILENAME_MAX_LENGTH 255

#define ASSOOFS_LAST_RESERVED_BLOCK ASSOOFS_ROOTDIR_BLOCK_NUMBER
#define ASSOOFS_LAST_RESERVED_INODE ASSOOFS_ROOTDIR_INODE_NUMBER

/**
 * Some macros for easy reading
 */
#define info(fmt) printk(KERN_INFO ASSOOFS_NAME ": " fmt)
#define info1(fmt, arg1) printk(KERN_INFO ASSOOFS_NAME ": " fmt, arg1)
#define info2(fmt, arg1, arg2) printk(KERN_INFO ASSOOFS_NAME ": " fmt, arg1, arg2)
#define info3(fmt, arg1, arg2, arg3) printk(KERN_INFO ASSOOFS_NAME ": " fmt, arg1, arg2, arg3)

#define error(fmt) printk(KERN_ERR ASSOOFS_NAME ": " fmt)
#define error1(fmt, arg1) printk(KERN_ERR ASSOOFS_NAME ": " fmt, arg1)
#define error2(fmt, arg1, arg2) printk(KERN_ERR ASSOOFS_NAME ": " fmt, arg1, arg2)
#define error3(fmt, arg1, arg2, arg3) printk(KERN_ERR ASSOOFS_NAME ": " fmt, arg1, arg2, arg3)

/**
 * Superblock structure
 */
struct assoofs_super_block
{
    uint64_t magic;
    uint64_t version;
    uint64_t block_size;
    uint64_t inodes_count;
    uint64_t free_blocks;

    char padding[4056];
};

/**
 * Directory structure
 */
struct assoofs_dir_record_entry
{
    char filename[ASSOOFS_FILENAME_MAX_LENGTH];
    uint64_t inode_no;
};

/**
 * Inode structure
 */
struct assoofs_inode
{
    mode_t mode;
    uint64_t inode_no;
    uint64_t data_block_number;

    union
    {
        uint64_t file_size;
        uint64_t dir_children_count;
    };
};

/**
 * Function declarations
 */
static int __init assoofs_init(void);
static void __exit assoofs_exit(void);

static void assoofs_kill_block_super(struct super_block *sb);

static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
int assoofs_fill_super(struct super_block *sb, void *data, int silent);

/**
 * Some static structures
 */
static struct file_system_type assoofs_type = {
    .owner = THIS_MODULE,
    .name = ASSOOFS_NAME,
    .mount = assoofs_mount,
    .kill_sb = assoofs_kill_block_super,
};

static struct super_operations assoofs_sb_ops = {
    .drop_inode = assoofs_delete_inode,
};

static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

static struct file_operations assoofs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

static struct file_operations assoofs_file_ops = {
    .read = assoofs_read,
    .write = assoofs_write,
};

/**
 * Register the load and unload functions
 */
module_init(assoofs_init);
module_exit(assoofs_exit);

/**
 * Some module metadata
 */
MODULE_AUTHOR("msahes00");
MODULE_LICENSE("GPL");