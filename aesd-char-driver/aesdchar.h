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

struct aesd_dev
{
    /**
     * TODO: Add structure(s) and locks needed to complete assignment requirements     
     */
    //modification start 
    #define AESD_HISTORY_MAX 10
    char   *cmd_data[AESD_HISTORY_MAX];
    size_t  cmd_len[AESD_HISTORY_MAX];
    size_t  head;        /* index of oldest entry when cmd_count==AESD_HISTORY_MAX */
    size_t  cmd_count;   /* number of valid entries (0..10) */
    size_t  total_len;   /* sum of lengths of all valid entries, for read/seek */

    /* in-progress (not yet newline-terminated) write accumulation */
    char   *partial_buf;
    size_t  partial_len;

    /* lock to serialize read/write/open/close */
    struct mutex *lock;
    //modification end
     
    struct cdev cdev;     /* Char device structure      */
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
