/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif


#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */
    
   if (!buffer || !entry_offset_byte_rtn) return NULL;

    size_t cumulative = 0;
    uint8_t idx = buffer->out_offs;

    /* Walk valid entries from oldest->newest.
       When buffer->full is true, we must traverse all AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED entries.
       When not full, we stop when idx reaches in_offs. */
    while (true) {
        struct aesd_buffer_entry *e = &buffer->entry[idx];

        if (e->buffptr && e->size) {
            if (char_offset < cumulative + e->size) {
                *entry_offset_byte_rtn = char_offset - cumulative;
                return e;
            }
            cumulative += e->size;
        }

        /* move to next slot */
        idx = (idx + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        /* termination conditions */
        if (!buffer->full && (idx == buffer->in_offs)) {
            /* we’ve consumed all valid entries */
            break;
        }
        if (buffer->full && (idx == buffer->out_offs)) {
            /* we wrapped and visited every entry once */
            break;
        }
    }
        
    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
 
  if (!buffer || !add_entry) return;

    /* If full, we are about to overwrite the oldest entry.
       Advance out_offs first so it stays pointing at the oldest entry after the write. */
    if (buffer->full) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    /* Write new entry at in_offs */
    buffer->entry[buffer->in_offs] = *add_entry;

    /* Advance in_offs */
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    /* Update full flag */
    buffer->full = (buffer->in_offs == buffer->out_offs);   
        
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
