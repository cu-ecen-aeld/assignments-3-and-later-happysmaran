/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#include <linux/mutex.h>           // For struct mutex
#include <linux/cdev.h>            // For struct cdev
#include "aesd-circular-buffer.h"  // For struct aesd_circular_buffer

#define AESD_DEBUG 1  

// ... (Your PDEBUG macros stay here) ...

struct aesd_dev
{
    struct aesd_circular_buffer buffer;
    char *partial_entry_ptr;
    size_t partial_entry_size;
    struct mutex lock;
    struct cdev cdev;
};

extern struct file_operations aesd_fops;

// Add these prototypes so the compiler knows the signatures for the fops
int aesd_open(struct inode *inode, struct file *filp);
int aesd_release(struct inode *inode, struct file *filp);
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
long aesd_adjust_file_offset(struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset);

#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
