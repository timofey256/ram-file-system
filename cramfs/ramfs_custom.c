#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/mount.h>
#include <linux/version.h>
#include <linux/pagemap.h>
#include <linux/statfs.h> 

#undef  RAMFS_MAGIC
#define RAMFSC_MAGIC   0xDEC0AD1A
#define HELLO_STR     "Hello from RAMFS ✨\n"
#define HELLO_NAME    "hello"
#define PAGE_ORDER    0           // one 4-KiB page

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal RAM-backed FS, kernel-6.12-ready");

struct mnt_idmap;        
extern struct mnt_idmap nop_mnt_idmap;

/* minimal super-block ops */
static const struct super_operations rf_sops = {
        .statfs      = simple_statfs,
        .drop_inode  = generic_delete_inode,
};

/* ---------------- file ops ---------------- */
static int rf_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t rf_read(struct file *f, char __user *buf,
                       size_t len, loff_t *ppos)
{
	char *data = f->private_data;
	size_t sz  = i_size_read(file_inode(f));
	return simple_read_from_buffer(buf, len, ppos, data, sz);
}

static ssize_t rf_write(struct file *f, const char __user *buf,
                        size_t len, loff_t *ppos)
{
	char *data = f->private_data;
	size_t max = PAGE_SIZE - 1;

	if (*ppos >= max)
		return -ENOSPC;
	if (len > max - *ppos)
		len = max - *ppos;

	if (copy_from_user(data + *ppos, buf, len))
		return -EFAULT;

	*ppos += len;
	data[*ppos] = '\0';
	i_size_write(file_inode(f), *ppos);
	return len;
}


static const struct file_operations rf_fops = {
    .open    = rf_open,
	.read    = rf_read,
	.write   = rf_write,
	.llseek  = generic_file_llseek,
};

/* ------------------------------------------- */

static struct inode *rf_make_inode(struct super_block *sb, umode_t mode)
{
	struct inode *inode = new_inode(sb);
	if (!inode)
		return NULL;

    inode_init_owner(&nop_mnt_idmap, inode, NULL, mode);

	//inode->i_mtime = current_time(inode);

	if (S_ISDIR(mode)) {
		inode->i_op  = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
	} else {
		inode->i_fop = &rf_fops;
		inode->i_mapping->a_ops = &empty_aops;
	}
	return inode;
}

/* superblock initialization */
static int rf_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root, *hello;
	struct dentry *hello_d;
	char *page;

	sb->s_op = &rf_sops;
	sb->s_magic = RAMFSC_MAGIC;
	sb->s_time_gran = 1;

	// initialize root directory
	root = rf_make_inode(sb, S_IFDIR | 0755);
	if (!root)
		return -ENOMEM;
	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;

	// initialize our single regular hello file
	hello = rf_make_inode(sb, S_IFREG | 0644);
	if (!hello)
		return -ENOMEM;

	page = (char *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, PAGE_ORDER);
	if (!page)
		return -ENOMEM;
	strcpy(page, HELLO_STR);
	i_size_write(hello, strlen(page));
	hello->i_private = page;

	hello_d = d_alloc_name(sb->s_root, HELLO_NAME);
	if (!hello_d)
		return -ENOMEM;
	d_add(hello_d, hello);
	return 0;
}

static struct dentry *rf_mount(struct file_system_type *t,
                               int flags, const char *dev, void *data)
{
	return mount_nodev(t, flags, data, rf_fill_super);
}

static void rf_kill_sb(struct super_block *sb)
{
    // This will clean up resources on unmount

	struct dentry *d = d_lookup(sb->s_root,
		&(const struct qstr)QSTR_INIT(HELLO_NAME, strlen(HELLO_NAME)));
	if (d) {
		free_pages_exact(d_inode(d)->i_private, PAGE_SIZE);
		dput(d);
	}
	kill_litter_super(sb);
}

// Main struct which describes our file system - so-called loader stub
//_`mount -t ramfs_custom none /mnt`_ calls `rf_mount`, _umount_ calls `rf_kill_sb`.
static struct file_system_type rf_fs_type = {
	.owner   = THIS_MODULE,
	.name    = "ramfs_custom",
	.mount   = rf_mount,
	.kill_sb = rf_kill_sb,
};

static int __init rf_init(void)   { return register_filesystem(&rf_fs_type); }
static void __exit rf_exit(void)  { unregister_filesystem(&rf_fs_type); }

module_init(rf_init);
module_exit(rf_exit);
