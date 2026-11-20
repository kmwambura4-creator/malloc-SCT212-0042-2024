#define _DEFAULT_SOURCE
#include <unistd.h>
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
/* Global pointers to manage the linked list */
static block_t *head = NULL;
static block_t *tail = NULL;

/* Total memory obtained from the operating system */
static size_t heap_bytes = 0;

/* Magic number used to identify valid blocks */
#define MAGIC 0x4D59414C
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
/* Request more memory from the operating system */
static block_t *grow(size_t need)
{
    /* Minimum amount of memory to request: 64 KB */
    size_t chunk_size = 64 * 1024;

    if (need > chunk_size) {
        chunk_size = need;
    }

    /* Align the requested payload size */
    chunk_size = align_up(chunk_size);

    /* Read the current program break */
    void *current_break = sbrk(0);

    if (current_break == (void *)-1) {
        return NULL;
    }

    /* Calculate padding to align the payload to 16 bytes */
    size_t padding =
        (16 - (((uintptr_t)current_break + HDR) % 16)) % 16;

    /* Check for size overflow */
    if (chunk_size > SIZE_MAX - HDR - padding) {
        return NULL;
    }

    /* Calculate total memory required */
    size_t total_size = padding + HDR + chunk_size;

    /* Request memory from the operating system */
    void *memory = sbrk(total_size);

    if (memory == (void *)-1) {
        return NULL;
    }

    /* Create the new block header */
    block_t *new_block = (block_t *)((char *)memory + padding);

    new_block->size = chunk_size;
    new_block->next = NULL;
    new_block->free = 1;
    new_block->magic = MAGIC;

    /* Add the new block to the linked list */
    if (head == NULL) {
        head = new_block;
    } else {
        tail->next = new_block;
    }

    tail = new_block;

    /* Update total heap memory */
    heap_bytes += total_size;

    return new_block;
}