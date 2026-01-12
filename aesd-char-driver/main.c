/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> 
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"

int aesd_major =   0;
int aesd_minor =   0;

MODULE_AUTHOR("happysmaran");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

// Prototypes to satisfy compiler warnings
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long aesd_adjust_file_offset(struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset);
int aesd_init_module(void);
void aesd_cleanup_module(void);

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte_rtn = 0;
    ssize_t retval = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Translate global f_pos to a specific buffer entry and offset within that entry
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset_byte_rtn);

    if (entry == NULL) {
        mutex_unlock(&dev->lock);
        return 0; 
    }

    size_t remaining_in_entry = entry->size - entry_offset_byte_rtn;
    size_t bytes_to_copy = (remaining_in_entry < count) ? remaining_in_entry : count;

    if (copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, bytes_to_copy)) {
        retval = -EFAULT;
    } else {
        retval = bytes_to_copy;
        *f_pos += bytes_to_copy; // Move the pointer forward for the next read call
    }

    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    char *new_data;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    new_data = krealloc(dev->partial_entry_ptr, dev->partial_entry_size + count, GFP_KERNEL);
    if (!new_data) {
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }
    dev->partial_entry_ptr = new_data;

    if (copy_from_user(dev->partial_entry_ptr + dev->partial_entry_size, buf, count)) {
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }
    dev->partial_entry_size += count;

    if (dev->partial_entry_ptr[dev->partial_entry_size - 1] == '\n') {
        struct aesd_buffer_entry new_entry;
        new_entry.buffptr = dev->partial_entry_ptr;
        new_entry.size = dev->partial_entry_size;

        const char *overwritten_ptr = aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
		if (overwritten_ptr) {
			kfree(overwritten_ptr);
		}

        dev->partial_entry_ptr = NULL;
        dev->partial_entry_size = 0;
    }

    mutex_unlock(&dev->lock);
    return count;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t total_size = 0;
    struct aesd_buffer_entry *entry;
    uint8_t index;
    loff_t retval;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Calculate total size of all content
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
        total_size += entry->size;
    }

    retval = fixed_size_llseek(filp, offset, whence, total_size);

    mutex_unlock(&dev->lock);
    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;
    struct aesd_seekto seekto;

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC || _IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    if (cmd == AESDCHAR_IOCSEEKTO) {
        if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(seekto)))
            return -EFAULT;

        // Perform the adjustment logic inside the helper function
        retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
        return retval;
    }
    return -ENOTTY;
}

long aesd_adjust_file_offset(struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset)
{
    struct aesd_dev *dev = filp->private_data;
    size_t total_offset = 0;
    int i;
    uint32_t total_commands = 0;
    struct aesd_buffer_entry *entry;
    uint8_t index;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // 1. Calculate how many valid entries we actually have
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
        if (entry->buffptr != NULL) total_commands++;
    }

    // 2. Validate input bounds
    if (write_cmd >= total_commands || 
        write_cmd_offset >= dev->buffer.entry[(dev->buffer.out_offs + write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    // 3. Calculate absolute position
    for (i = 0; i < write_cmd; i++) {
        total_offset += dev->buffer.entry[(dev->buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size;
    }
    total_offset += write_cmd_offset;

    // 4. Update file position and exit
    filp->f_pos = total_offset;
    mutex_unlock(&dev->lock);
    return 0;
}

struct file_operations aesd_fops = {
    .owner =          THIS_MODULE,
    .read =           aesd_read,
    .write =          aesd_write,
    .open =           aesd_open,
    .release =        aesd_release,
    .llseek =         aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);
    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) return result;

    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);
    if (result) unregister_chrdev_region(dev, 1);
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    uint8_t index;
    struct aesd_buffer_entry *entry;
    
    cdev_del(&aesd_device.cdev);
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        kfree(entry->buffptr);
    }
    kfree(aesd_device.partial_entry_ptr);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
