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
#include <linux/limits.h> 
#include <linux/list.h> 

#define RAMFSC_MAGIC   0xDEC0AD1A

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal RAM-backed FS, kernel-6.12-ready");

struct mnt_idmap;        
extern struct mnt_idmap nop_mnt_idmap;

static const struct file_operations  rf_fops;
static const struct inode_operations rf_dir_iops;
static const struct super_operations rf_sops;

/* File RAM buffer */
struct rbuf {
	char  *data;
	size_t size;      // bytes used
	size_t cap;       // bytes allocated
};

static int rf_reserve(struct rbuf *rb, size_t need) {
    if (need <= rb->cap)
        return 0;

    size_t newcap = rb->cap ? rb->cap : PAGE_SIZE;
    while (newcap < need)
        newcap <<= 1;

    char *tmp = krealloc(rb->data, newcap, GFP_KERNEL);
    if (!tmp)
        return -ENOMEM;

    rb->data = tmp;
    rb->cap = newcap;
    // don't touch rb->size because it's just an allocation. we don't yet know how much data we will store there.
    return 0;
}



static int rf_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t rf_read(struct file *f, char __user *buf,
                       size_t len, loff_t *ppos)
{
    struct rbuf *rb = f->private_data;
	return simple_read_from_buffer(buf, len, ppos, rb->data, rb->size);
}

static int rf_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
                      struct iattr *attr)
{
    struct inode *inode = d_inode(dentry);

    /* Let generic helper perform all normal work */
    int err = simple_setattr(idmap, dentry, attr);
    if (err || !(attr->ia_valid & ATTR_SIZE))
            return err;

    /* Only regular files have a rbuf */
    if (S_ISREG(inode->i_mode)) {
            struct rbuf *rb = inode->i_private;
            loff_t new = i_size_read(inode);

            if (new > rb->cap && rf_reserve(rb, new))
                    return -ENOMEM;
            rb->size = new;
    }
    return 0;
}

static ssize_t rf_write(struct file *f, const char __user *buf,
                        size_t len, loff_t *ppos)
{
	struct rbuf *rb = f->private_data;

    if (f->f_flags & O_APPEND)
        *ppos = rb->size;

    loff_t end = *ppos + len;

	if (end > INT_MAX) // sanity check
		return -EFBIG;
    if (rf_reserve(rb, end))
        return -ENOMEM;
    if (copy_from_user(rb->data + *ppos, buf, len))
        return -EFAULT;

	*ppos += len;
    rb->size = max_t(size_t, rb->size, end);
	i_size_write(file_inode(f), rb->size);
	return len;
}

static int rf_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
    /* Everything already lives in memory, nothing to flush. */
    return 0;
}

static const struct file_operations rf_fops = {
    .open    = rf_open,
	.read    = rf_read,
	.write   = rf_write,
	.llseek  = generic_file_llseek,
    .fsync   = rf_fsync,
};



static struct inode *rf_make_inode(struct super_block *sb, umode_t mode)
{
	struct inode *inode = new_inode(sb);
	if (!inode)
		return NULL;

    inode_init_owner(&nop_mnt_idmap, inode, NULL, mode);

	if (S_ISDIR(mode)) {
		inode->i_op  = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
	} else {
		inode->i_fop = &rf_fops;
		inode->i_mapping->a_ops = &empty_aops;
	}
	return inode;
}

static int rf_create(struct mnt_idmap *idmap, struct inode *dir,
                     struct dentry *dentry, umode_t mode, bool excl) {
    struct inode *ino = rf_make_inode(dir->i_sb, S_IFREG | mode);
	struct rbuf  *rb;

	if (!ino)
		return -ENOMEM;

	rb = kzalloc(sizeof(*rb), GFP_KERNEL);
	if (!rb || rf_reserve(rb, PAGE_SIZE)) {
		iput(ino);
		kfree(rb);
		return -ENOMEM;
	}
	ino->i_private = rb;

	d_add(dentry, ino);   /* bind dentry to inode */
	return 0;
}



static int rf_mkdir(struct mnt_idmap *idmap, struct inode *dir,
                    struct dentry *dentry, umode_t mode)
{
    struct inode *inode;

    inode = rf_make_inode(dir->i_sb, S_IFDIR | mode);
    if (!inode)
        return -ENOMEM;

    inode_inc_link_count(dir);
    inode_inc_link_count(inode);

    inode->i_op  = &rf_dir_iops;
    inode->i_fop = &simple_dir_operations;

    d_add(dentry, inode);

    return 0;
}


static const struct inode_operations rf_dir_iops = {
	.lookup = simple_lookup,
	.create = rf_create,
    .setattr = rf_setattr,
    .mkdir = rf_mkdir,
};



static void rf_evict(struct inode *inode)
{
    if (S_ISREG(inode->i_mode)) {
            struct rbuf *rb = inode->i_private;
            kfree(rb->data);
            kfree(rb);
    }
    truncate_inode_pages_final(&inode->i_data);
    clear_inode(inode);
}

static const struct super_operations rf_sops = {
    .statfs      = simple_statfs,
    .drop_inode  = generic_delete_inode,
    .evict_inode = rf_evict
};



static int rf_fill_super(struct super_block *sb, void *data, int silent)
{
	sb->s_op = &rf_sops;
	sb->s_magic = RAMFSC_MAGIC;
	sb->s_time_gran = 1;

	// initialize root directory
    struct inode *root;
	root = rf_make_inode(sb, S_IFDIR | 0755);
	if (!root)
		return -ENOMEM;
    root->i_op = &rf_dir_iops;

	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static struct dentry *rf_mount(struct file_system_type *t,
                               int flags, const char *dev, void *data)
{
	return mount_nodev(t, flags, data, rf_fill_super);
}

static struct file_system_type rf_fs_type = {
	.owner   = THIS_MODULE,
	.name    = "myramfs",
	.mount   = rf_mount,
	.kill_sb = kill_litter_super,
};

static int __init rf_init(void)   { return register_filesystem(&rf_fs_type); }
static void __exit rf_exit(void)  { unregister_filesystem(&rf_fs_type); }

module_init(rf_init);
module_exit(rf_exit);
