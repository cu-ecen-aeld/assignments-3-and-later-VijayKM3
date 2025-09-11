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

#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Vs321");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    filp->private_data = &aesd_device;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    size_t bytes_to_copy = 0;
    size_t bytes_copied = 0;
    size_t current_pos = 0;
    size_t i;
    
    PDEBUG("read %zu bytes from offset %lld", count, *f_pos);

    if (!dev)
        return -EIO;

    mutex_lock(&dev->lock);

    /* Check for EOF */
    if (*f_pos >= dev->total_len) {
        PDEBUG("EOF: f_pos=%lld >= total_len=%zu", *f_pos, dev->total_len);
        retval = 0;
        goto out_unlock;
    }

    /* Calculate how many bytes we can read */
    bytes_to_copy = min(count, (size_t)(dev->total_len - *f_pos));
    
    PDEBUG("Will copy %zu bytes (total_len=%zu, f_pos=%lld)", 
           bytes_to_copy, dev->total_len, *f_pos);

    /* Find starting position and copy data */
    for (i = 0; i < dev->cmd_count && bytes_copied < bytes_to_copy; i++) {
        size_t entry_idx = (dev->head + i) % AESD_HISTORY_MAX;
        size_t entry_len = dev->cmd_len[entry_idx];
        
        /* Check if we need to skip this entry */
        if (current_pos + entry_len <= *f_pos) {
            current_pos += entry_len;
            continue;
        }
        
        /* Calculate offset within this entry */
        size_t offset_in_entry = 0;
        if (*f_pos > current_pos) {
            offset_in_entry = *f_pos - current_pos;
        }
        
        /* Calculate how many bytes to copy from this entry */
        size_t bytes_from_entry = min(entry_len - offset_in_entry, 
                                      bytes_to_copy - bytes_copied);
        
        PDEBUG("Copying %zu bytes from entry %zu (offset %zu)", 
               bytes_from_entry, entry_idx, offset_in_entry);
        
        /* Copy to user space */
        if (copy_to_user(buf + bytes_copied,
                        dev->cmd_data[entry_idx] + offset_in_entry,
                        bytes_from_entry)) {
            retval = -EFAULT;
            goto out_unlock;
        }
        
        bytes_copied += bytes_from_entry;
        current_pos += entry_len;
    }
    
    *f_pos += bytes_copied;
    retval = bytes_copied;
    
    PDEBUG("Read complete: copied %zd bytes, new f_pos=%lld", retval, *f_pos);

out_unlock:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *write_buf = NULL;
    size_t i;
    
    PDEBUG("write %zu bytes", count);

    if (!dev)
        return -EIO;
    if (count == 0)
        return 0;

    /* Allocate kernel buffer for user data */
    write_buf = kmalloc(count, GFP_KERNEL);
    if (!write_buf)
        return -ENOMEM;

    /* Copy from user space */
    if (copy_from_user(write_buf, buf, count)) {
        kfree(write_buf);
        return -EFAULT;
    }

    mutex_lock(&dev->lock);

    /* Process the write buffer, looking for newlines */
    for (i = 0; i < count; i++) {
        /* Grow partial buffer */
        char *new_partial = krealloc(dev->partial_buf, 
                                     dev->partial_len + 1, 
                                     GFP_KERNEL);
        if (!new_partial) {
            retval = -ENOMEM;
            goto out_unlock;
        }
        dev->partial_buf = new_partial;
        dev->partial_buf[dev->partial_len] = write_buf[i];
        dev->partial_len++;
        
        /* Check for newline - complete command */
        if (write_buf[i] == '\n') {
            char *cmd_copy;
            size_t insert_idx;
            
            PDEBUG("Complete command of %zu bytes", dev->partial_len);
            
            /* Make a copy of the complete command */
            cmd_copy = kmalloc(dev->partial_len, GFP_KERNEL);
            if (!cmd_copy) {
                retval = -ENOMEM;
                goto out_unlock;
            }
            memcpy(cmd_copy, dev->partial_buf, dev->partial_len);
            
            /* Handle circular buffer */
            if (dev->cmd_count == AESD_HISTORY_MAX) {
                /* Buffer is full - remove oldest entry */
                PDEBUG("Buffer full, removing oldest entry at %zu", dev->head);
                kfree(dev->cmd_data[dev->head]);
                dev->total_len -= dev->cmd_len[dev->head];
                dev->head = (dev->head + 1) % AESD_HISTORY_MAX;
                dev->cmd_count--;
            }
            
            /* Insert new command */
            insert_idx = (dev->head + dev->cmd_count) % AESD_HISTORY_MAX;
            dev->cmd_data[insert_idx] = cmd_copy;
            dev->cmd_len[insert_idx] = dev->partial_len;
            dev->total_len += dev->partial_len;
            dev->cmd_count++;
            
            PDEBUG("Inserted at %zu, count=%zu, total_len=%zu", 
                   insert_idx, dev->cmd_count, dev->total_len);
            
            /* Clear partial buffer */
            kfree(dev->partial_buf);
            dev->partial_buf = NULL;
            dev->partial_len = 0;
        }
    }
    
    retval = count;

out_unlock:
    mutex_unlock(&dev->lock);
    kfree(write_buf);
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
    
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /* Initialize the AESD specific portion of the device */
    aesd_device.head = 0;
    aesd_device.cmd_count = 0;
    aesd_device.total_len = 0;
    aesd_device.partial_buf = NULL;
    aesd_device.partial_len = 0;
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);
    if (result) {
        unregister_chrdev_region(dev, 1);
    }
    
    printk(KERN_INFO "aesdchar driver initialized\n");
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);
    size_t i;

    cdev_del(&aesd_device.cdev);

    /* Cleanup AESD specific portions */
    mutex_lock(&aesd_device.lock);
    
    /* Free all command buffers */
    for (i = 0; i < aesd_device.cmd_count; i++) {
        size_t idx = (aesd_device.head + i) % AESD_HISTORY_MAX;
        kfree(aesd_device.cmd_data[idx]);
        aesd_device.cmd_data[idx] = NULL;
    }

    /* Free partial buffer */
    kfree(aesd_device.partial_buf);
    aesd_device.partial_buf = NULL;
    
    mutex_unlock(&aesd_device.lock);
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
    printk(KERN_INFO "aesdchar driver cleanup complete\n");
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
