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
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesd-circular-buffer.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("happysmaran");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
	struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    // ignore
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte = 0;
    ssize_t bytes_to_read = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset_byte);

    if (entry) {
        size_t available = entry->size - entry_offset_byte;
        bytes_to_read = (available < count) ? available : count;

        if (copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_read)) {
            bytes_to_read = -EFAULT;
        } else {
            *f_pos += bytes_to_read;
        }
    }

    mutex_unlock(&dev->lock);
    return bytes_to_read;
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
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
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
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
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
	aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    uint8_t index;
	struct aesd_buffer_entry *entry;
	
	AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
		kfree(entry->buffptr);
	}
	
	kfree(aesd_device.partial_entry_ptr);

    unregister_chrdev_region(devno, 1);
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t total_size = 0;
    struct aesd_buffer_entry *entry;
    uint8_t index;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
        total_size += entry->size;
    }

    loff_t new_pos = fixed_size_llseek(filp, offset, whence, total_size);
    
    mutex_unlock(&dev->lock);
    return new_pos;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC || _IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;

    if (cmd == AESDCHAR_IOCSEEKTO) {
        if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(seekto)))
            return -EFAULT;

        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;

        long retval = 0;
        uint32_t total_commands = 0;
        struct aesd_buffer_entry *entry;
        uint8_t index;
        
 
        AESD_CIRCULAR_BUFFER_FOREACH(entry, &dev->buffer, index) {
            total_commands++;
        }

        if (seekto.write_cmd >= total_commands || 
            seekto.write_cmd_offset >= dev->buffer.entry[
                (dev->buffer.out_offs + seekto.write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
            ].size) {
            retval = -EINVAL;
        } else {
            loff_t final_pos = 0;
            for (int i = 0; i < seekto.write_cmd; i++) {
                final_pos += dev->buffer.entry[(dev->buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED].size;
            }
            final_pos += seekto.write_cmd_offset;
            filp->f_pos = final_pos;
        }

        mutex_unlock(&dev->lock);
        return retval;
    }
    return -ENOTTY;
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
