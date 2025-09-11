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

//modifications start
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
//modifications end

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Vs321"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
     /* bind this file instance to our single device */
    
    //modifications start
    filp->private_data = &aesd_device;

    /* lazily allocate/init the device mutex once */
   // if (!aesd_device.lock) {
   //     struct mutex *m = kmalloc(sizeof(*m), GFP_KERNEL);
   //     if (!m)
   //         return -ENOMEM;
   //     mutex_init(m);
   //     aesd_device.lock = m;
   // }
    //modifications end
     
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    //modifications start
    //modifications end
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = filp->private_data;
    size_t copied = 0;
    size_t idx, i;
    size_t pos = 0; /* cumulative position in the history */

    if (!dev)
        return -EIO;

    mutex_lock(&dev->lock);

if (dev->cmd_count > AESD_HISTORY_MAX) {
    pr_warn("aesd: cmd_count corrupt: %zu, resetting\n", dev->cmd_count);
    dev->cmd_count = AESD_HISTORY_MAX;
}

    if (*f_pos >= dev->total_len) {
        retval = 0; /* EOF */
        goto out_unlock;
    }

    /* Walk through commands in order */
    idx = dev->head;
    for (i = 0; i < dev->cmd_count && copied < count; i++) {
        size_t entry = (idx + i) % AESD_HISTORY_MAX;
        size_t elen  = dev->cmd_len[entry];

        if (pos + elen <= *f_pos) {
            /* skip this entry completely */
            pos += elen;
            continue;
        }

        /* calculate start position inside this entry */
        size_t start_in_entry = (*f_pos > pos) ? (*f_pos - pos) : 0;
        size_t avail_in_entry = elen - start_in_entry;
        size_t take = (count - copied < avail_in_entry) ? (count - copied) : avail_in_entry;

        if (copy_to_user(buf + copied,
                         dev->cmd_data[entry] + start_in_entry,
                         take)) {
            retval = -EFAULT;
            goto out_unlock;
        }

        copied += take;
        *f_pos += take;
        pos += elen;
    }

    retval = copied;

out_unlock:
    mutex_unlock(&dev->lock);
    return retval;
}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    char *ubuf = NULL;
    size_t off = 0;

    if (!dev)
        return -EIO;
    if (count == 0)
        return 0;

    ubuf = kmalloc(count, GFP_KERNEL);
    if (!ubuf)
        return -ENOMEM;

    if (copy_from_user(ubuf, buf, count)) {
        kfree(ubuf);
        return -EFAULT;
    }

    mutex_lock(&dev->lock);

    /* Helper to append (bytes [s..e)) to dev->partial_buf */
/* Helper to append (bytes [s..e)) to dev->partial_buf */
#define APPEND_TO_PARTIAL(s, add)                                      \
    do {                                                               \
        size_t _newlen = dev->partial_len + (add);                     \
        char *nb = kmalloc(_newlen + 1, GFP_KERNEL);                   \
        if (!nb) { retval = -ENOMEM; goto out_unlock_free; }           \
        if (dev->partial_len)                                           \
            memcpy(nb, dev->partial_buf, dev->partial_len);            \
        memcpy(nb + dev->partial_len, (s), (add));                     \
        nb[_newlen] = '\0';                                            \
        kfree(dev->partial_buf);                                       \
        dev->partial_buf = nb;                                         \
        dev->partial_len = _newlen;                                    \
    } while (0)

/* Helper to push a COMPLETE command in history (freeing oldest if needed) */
#define PUSH_COMPLETE()                                                     \
 do {                                                                       \
    size_t ins_idx;                                                         \
    char *final;                                                            \
    /* allocate exact bytes + NUL so the stored buffer is always safe */    \
    final = kmalloc(dev->partial_len + 1, GFP_KERNEL);                      \
    if (!final) { retval = -ENOMEM; goto out_unlock_free; }                 \
    memcpy(final, dev->partial_buf, dev->partial_len);                      \
    final[dev->partial_len] = '\0';                                         \
    /* if full, drop oldest */                                              \
    if (dev->cmd_count == AESD_HISTORY_MAX) {                               \
        kfree(dev->cmd_data[dev->head]);                                    \
        dev->total_len -= dev->cmd_len[dev->head];                          \
        dev->head = (dev->head + 1) % AESD_HISTORY_MAX;                     \
    } else {                                                                 \
        dev->cmd_count++;                                                    \
    }                                                                        \
    ins_idx = (dev->head + dev->cmd_count - 1) % AESD_HISTORY_MAX;           \
    dev->cmd_data[ins_idx] = final;                                         \
    dev->cmd_len[ins_idx]  = dev->partial_len;                              \
    dev->total_len        += dev->partial_len;                              \
    pr_info("aesd: PUSH_COMPLETE idx=%zu count=%zu total_len=%zu len=%zu\n",\
           ins_idx, dev->cmd_count, dev->total_len, dev->cmd_len[ins_idx]);  \
    kfree(dev->partial_buf);                                                 \
    dev->partial_buf = NULL;                                                 \
    dev->partial_len = 0;                                                    \
 } while (0)

    /* Process the user buffer, possibly creating multiple complete commands
     * split on '\n'. Bytes after the last '\n' remain in partial_buf. */
    while (off < count) {
        void *nlp = memchr(ubuf + off, '\n', count - off);

        if (!nlp) {
            /* no newline: append all remaining data to partial */
            APPEND_TO_PARTIAL(ubuf + off, count - off);
            off = count;
            break;
        } else {
            /* found a newline: append chunk INCLUDING newline */
            size_t upto = ((char *)nlp - (ubuf + off)) + 1;
            APPEND_TO_PARTIAL(ubuf + off, upto);
            off += upto;

            /* now we have a complete command: push into history */
            PUSH_COMPLETE();
        }
    }

    retval = count;  /* report number of bytes accepted */

out_unlock_free:
    mutex_unlock(&dev->lock);
    kfree(ubuf);
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

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    //modifications start
    /* initialize AESD-specific device state */
    aesd_device.head       = 0;
    aesd_device.cmd_count  = 0;
    aesd_device.total_len  = 0;
    aesd_device.partial_buf = NULL;
    aesd_device.partial_len = 0;
   // aesd_device.lock        = NULL; /* allocated lazily in open() */
    /* initialize the embedded mutex */
    mutex_init(&aesd_device.lock);
    //modifications end
    
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

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    //modifications start
    size_t i;

    /* free all history entries */
    for (i = 0; i < aesd_device.cmd_count; i++) {
        size_t idx = (aesd_device.head + i) % AESD_HISTORY_MAX;
        kfree(aesd_device.cmd_data[idx]);
        aesd_device.cmd_data[idx] = NULL;
        aesd_device.cmd_len[idx]  = 0;
    }

    /* free any in-progress partial buffer */
    kfree(aesd_device.partial_buf);
    aesd_device.partial_buf = NULL;
    aesd_device.partial_len = 0;

    /* free the mutex if allocated */
   // if (aesd_device.lock) {
   //     /* no explicit destroy needed for struct mutex */
   //     kfree(aesd_device.lock);
   //     aesd_device.lock = NULL;
   // }
    //modifications end
    
    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
