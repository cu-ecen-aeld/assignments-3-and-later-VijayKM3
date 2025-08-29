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
    
    size_t cumulative_size = 0;
    int i;
    
    // Check for null pointers
    if (buffer == NULL || entry_offset_byte_rtn == NULL) {
        return NULL;
    }

    // Iterate through the buffer, starting from the out_offs
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        int index = (buffer->out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        struct aesd_buffer_entry *entry = &buffer->entry[index];
        
        // Break the loop if we reach the end of the written data
        if (entry->buffptr == NULL && cumulative_size == 0) {
            return NULL;
        }

        if (char_offset < (cumulative_size + entry->size)) {
            // Found the correct entry
            *entry_offset_byte_rtn = char_offset - cumulative_size;
            return entry;
        }

        cumulative_size += entry->size;
        
        // Stop if we have iterated past the "in_offs" and the buffer is not full
        if (index == buffer->in_offs && cumulative_size > 0 &&
            (buffer->in_offs != buffer->out_offs || cumulative_size != buffer->total_size)) {
            return NULL;
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
    
    // Check for null pointers
    if (buffer == NULL || add_entry == NULL) {
        return;
    }

    // Check if the buffer is full (in_offs is about to overwrite out_offs)
    if (buffer->in_offs == buffer->out_offs && buffer->total_size > 0) {
        // Free the memory of the entry being overwritten
        if (buffer->entry[buffer->out_offs].buffptr != NULL) {
            kfree(buffer->entry[buffer->out_offs].buffptr);
        }
    }

    // Add the new entry at the current in_offs position
    buffer->entry[buffer->in_offs] = *add_entry;
    
    // Update the total size
    buffer->total_size += add_entry->size;

    // Advance in_offs, wrapping around the buffer
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    
    // If the new in_offs is now equal to out_offs, it means the buffer is full, so advance out_offs
    if (buffer->in_offs == buffer->out_offs) {
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }
     
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
