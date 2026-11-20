#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "myalloc.h"

/* Structure representing one memory block */
typedef struct block {
    size_t size;          // Size of the memory block
    struct block *next;   // Pointer to the next block
    int free;             // 1 = free, 0 = allocated
    int magic;            // Used to identify valid blocks
} block_t;

/* Round the header size up to a multiple of 16 */
#define HDR ((sizeof(block_t) + 15) & ~(size_t)15)

/* Round a requested size up to a multiple of 16 */
static inline size_t align_up(size_t n)
{
    return (n + 15) & ~(size_t)15;
}

/* Convert a block header address to its payload address */
static inline void *payload(block_t *block)
{
    return (char *)block + HDR;
}

/* Convert a payload address back to its block header */
static inline block_t *header(void *ptr)
{
    return (block_t *)((char *)ptr - HDR);
}