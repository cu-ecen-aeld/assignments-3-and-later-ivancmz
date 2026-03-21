/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
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

#include <linux/mutex.h>
#include "aesd-circular-buffer.h"

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements
     *
     * Implementation:
     *   - buffer:       circular buffer holding the last 10 completed write commands
     *   - lock:         mutex serialising all read/write file operations
     *   - partial_buf:  accumulates write data that has not yet been terminated
     *                   by a '\n'; appended on each write until '\n' is seen
     *   - partial_size: current byte count in partial_buf
     */
    struct aesd_circular_buffer buffer;
    struct mutex                lock;
    char                       *partial_buf;
    size_t                      partial_size;
    struct cdev cdev;     /* Char device structure      */
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
