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
#include <linux/slab.h>    /* kmalloc / kfree / krealloc */
#include <linux/uaccess.h> /* copy_to_user / copy_from_user */
#include <linux/string.h>  /* memchr */
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ivan Marquez"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     *
     * Implementation:
     *   Use container_of to recover the aesd_dev that owns this cdev, then
     *   store it in filp->private_data so every subsequent read/write can
     *   reach it without a global variable.
     */
    filp->private_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     *
     * Implementation:
     *   No per-open state was allocated in open(), so nothing needs to be
     *   freed here.  Returning 0 is sufficient.
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     *
     * Implementation:
     *   1. Acquire the device mutex (interruptible so that signals can abort).
     *   2. Ask the circular buffer which entry contains the byte at *f_pos
     *      and how far into that entry the offset falls.
     *   3. Clamp the transfer to the remaining bytes in that one entry and
     *      to the caller's count limit.
     *   4. copy_to_user the slice; advance *f_pos by the number of bytes
     *      actually copied; return that count.
     *   5. Returning fewer bytes than count is fine – the caller (e.g. cat)
     *      will keep calling read() until 0 is returned (EOF), which happens
     *      when find_entry_offset_for_fpos() returns NULL because *f_pos is
     *      past all stored data.
     */
    struct aesd_dev         *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t                   entry_offset;
    size_t                   bytes_to_copy;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                &dev->buffer, (size_t)*f_pos, &entry_offset);
    if (!entry)
        goto out; /* retval stays 0 → EOF */

    bytes_to_copy = entry->size - entry_offset;
    if (bytes_to_copy > count)
        bytes_to_copy = count;

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy)) {
        retval = -EFAULT;
        goto out;
    }

    *f_pos += (loff_t)bytes_to_copy;
    retval   = (ssize_t)bytes_to_copy;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     *
     * Implementation:
     *   1. Copy the user buffer into a kernel-side temporary buffer.
     *   2. Acquire the device mutex to serialise concurrent writers.
     *   3. Append the new bytes to dev->partial_buf (krealloc grows it).
     *      Writes that do not end with '\n' are saved here and will be
     *      completed by a future write() call.
     *   4. Search partial_buf for '\n'.  When found:
     *      a. kmalloc a permanent buffer for the complete command (up to and
     *         including the '\n') and copy it there.
     *      b. If the circular buffer is already full the oldest entry's memory
     *         would be overwritten – free it first to avoid a leak.
     *      c. Hand the new entry to aesd_circular_buffer_add_entry().
     *      d. If there were bytes after the '\n' in this write, move them into
     *         a fresh partial_buf so they start the next command; otherwise
     *         just clear partial_buf.
     *   5. Always return count (all bytes were consumed even if they only
     *      extended the partial buffer).
     */
    struct aesd_dev *dev = filp->private_data;
    char            *kern_buf;
    char            *new_partial;
    const char      *newline_pos;
    size_t           new_size;

    kern_buf = kmalloc(count, GFP_KERNEL);
    if (!kern_buf)
        return -ENOMEM;

    if (copy_from_user(kern_buf, buf, count)) {
        kfree(kern_buf);
        return -EFAULT;
    }

    if (mutex_lock_interruptible(&dev->lock)) {
        kfree(kern_buf);
        return -ERESTARTSYS;
    }

    /* Append incoming bytes to the partial-command accumulation buffer */
    new_size    = dev->partial_size + count;
    new_partial = krealloc(dev->partial_buf, new_size, GFP_KERNEL);
    if (!new_partial) {
        kfree(kern_buf);
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }
    memcpy(new_partial + dev->partial_size, kern_buf, count);
    kfree(kern_buf);
    dev->partial_buf  = new_partial;
    dev->partial_size = new_size;

    /* Check whether we now have a complete command (terminated by '\n') */
    newline_pos = memchr(dev->partial_buf, '\n', dev->partial_size);
    if (newline_pos) {
        struct aesd_buffer_entry new_entry;
        size_t cmd_size   = (size_t)(newline_pos - dev->partial_buf) + 1;
        size_t remaining  = dev->partial_size - cmd_size;
        char  *cmd_buf    = kmalloc(cmd_size, GFP_KERNEL);

        if (!cmd_buf) {
            mutex_unlock(&dev->lock);
            return -ENOMEM;
        }
        memcpy(cmd_buf, dev->partial_buf, cmd_size);

        new_entry.buffptr = cmd_buf;
        new_entry.size    = cmd_size;

        /*
         * If the circular buffer is full the slot at in_offs holds the oldest
         * entry whose memory we are about to overwrite – free it now.
         */
        if (dev->buffer.full)
            kfree(dev->buffer.entry[dev->buffer.in_offs].buffptr);

        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);

        /* Preserve any bytes that followed the '\n' in this write */
        if (remaining > 0) {
            char *leftover = kmalloc(remaining, GFP_KERNEL);
            if (leftover)
                memcpy(leftover, dev->partial_buf + cmd_size, remaining);
            kfree(dev->partial_buf);
            dev->partial_buf  = leftover; /* may be NULL on alloc failure */
            dev->partial_size = leftover ? remaining : 0;
        } else {
            kfree(dev->partial_buf);
            dev->partial_buf  = NULL;
            dev->partial_size = 0;
        }
    }

    retval = (ssize_t)count;
    mutex_unlock(&dev->lock);
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
     *
     * Implementation:
     *   - aesd_circular_buffer_init zeroes the buffer struct (in/out offsets,
     *     full flag, and all entry pointers).
     *   - mutex_init prepares the mutex for use.
     *   - partial_buf / partial_size are already zero from the memset above.
     */
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);

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
     *
     * Implementation:
     *   - Walk every slot in the circular buffer with the provided foreach
     *     macro and kfree any buffptr that was allocated by aesd_write.
     *   - kfree any leftover partial_buf that was never completed with '\n'.
     */
    {
        struct aesd_buffer_entry *entry;
        uint8_t index;
        AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
            if (entry->buffptr)
                kfree(entry->buffptr);
        }
    }
    kfree(aesd_device.partial_buf);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
