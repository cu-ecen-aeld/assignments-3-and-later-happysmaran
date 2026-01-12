/*
 * aesdchar.h
 *
 * Created on: Oct 23, 2019
 * Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include <linux/mutex.h>           // For struct mutex
#include <linux/cdev.h>            // For struct cdev
#include "aesd-circular-buffer.h"  // For struct aesd_circular_buffer

#define AESD_DEBUG 1  

#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev
{
    struct aesd_circular_buffer buffer;
    char *partial_entry_ptr;
    size_t partial_entry_size;
    struct mutex lock;
    struct cdev cdev;
};

extern struct file_operations aesd_fops;

// Function prototypes for file operations
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
loff_t aesd_llseek(struct file *filp, loff_t offset, int whence);
long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long aesd_adjust_file_offset(struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset);

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
