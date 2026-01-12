/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer implementation
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.
 * @param char_offset the position to search for in the buffer list.
 * @param entry_offset_byte_rtn pointer to store the byte offset within the returned entry.
 * @return the entry representing the position, or NULL if not available.
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    size_t cumulative_offset = 0;
    uint8_t index = buffer->out_offs;
    int count;
    
    // Calculate how many entries we actually have to iterate through
    int max_entries = 0;
    if (buffer->full) {
        max_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } else {
        if (buffer->in_offs >= buffer->out_offs) {
            max_entries = buffer->in_offs - buffer->out_offs;
        } else {
            max_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - (buffer->out_offs - buffer->in_offs);
        }
    }

    for (count = 0; count < max_entries; count++) {
        struct aesd_buffer_entry *entry = &buffer->entry[index];
        
        // Check if char_offset falls within this specific entry
        if (char_offset < (cumulative_offset + entry->size)) {
            if (entry_offset_byte_rtn) {
                *entry_offset_byte_rtn = char_offset - cumulative_offset;
            }
            return entry;
        }
        
        cumulative_offset += entry->size;
        index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    return NULL;
}

/**
* Adds entry to buffer, returning the buffptr of any overwritten entry for freeing.
*/
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char *ret_ptr = NULL;

    // If buffer is full, we are about to overwrite the oldest entry
    if (buffer->full) {
        ret_ptr = buffer->entry[buffer->out_offs].buffptr;
        // Advance out_offs as the oldest is being removed/overwritten
        buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // Add the new entry at the current in_offs
    buffer->entry[buffer->in_offs] = *add_entry;
    
    // Advance in_offs
    buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // If in_offs caught up to out_offs, we are now full
    if (buffer->in_offs == buffer->out_offs) {
        buffer->full = true;
    }

    return ret_ptr;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
