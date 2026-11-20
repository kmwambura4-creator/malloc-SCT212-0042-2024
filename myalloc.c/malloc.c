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
/* Find the first free block that can satisfy the request */
static block_t *find_fit(size_t need)
{
    block_t *current = head;

    while (current != NULL) {
        if (current->free == 1 && current->size >= need) {
            return current;
        }

        current = current->next;
    }

    return NULL;
}/* Allocate memory using the first-fit strategy */
/* Split a block when it has enough extra space */
static void split_block(block_t *block, size_t need)
{
    /* Check whether splitting is possible */
    if (block->size >= need + HDR + 16) {

        /* Calculate the size of the remaining block */
        size_t remaining_size = block->size - need - HDR;

        /* Locate where the new block header should begin */
        block_t *new_block =
            (block_t *)((char *)payload(block) + need);

        /* Initialize the new free block */
        new_block->size = remaining_size;
        new_block->free = 1;
        new_block->magic = MAGIC;
        new_block->next = block->next;

        /* Link the new block into the list */
        block->next = new_block;

        /* Update tail if the original block was the last block */
        if (tail == block) {
            tail = new_block;
        }

        /* Reduce the size of the original block */
        block->size = need;
    }
}
void *my_malloc(size_t size)
{
    /* Zero-byte requests return NULL */
    if (size == 0) {
        return NULL;
    }

    /* Prevent overflow during alignment */
    if (size > SIZE_MAX - 15) {
        return NULL;
    }

    /* Align the requested size to 16 bytes */
    size_t need = align_up(size);

    /* Search for a suitable free block */
    block_t *block = find_fit(need);

    /* If no suitable block exists, grow the heap */
    if (block == NULL) {
        if (grow(need) == NULL) {
            return NULL;
        }

        /* Search again after growing the heap */
        block = find_fit(need);

        if (block == NULL) {
            return NULL;
        }
    }

    /* Split the block if it has enough extra space */
split_block(block, need);

/* Mark the selected block as occupied */
block->free = 0;

/* Return the usable memory, not the header */
return payload(block);
}
/* Merge physically adjacent free blocks */
static void coalesce(void)
{
    block_t *current = head;

    while (current != NULL && current->next != NULL) {

        block_t *next_block = current->next;

        if (current->free == 1 && next_block->free == 1) {

            /* Check whether the blocks are physically adjacent */
            void *current_end =
                (char *)payload(current) + current->size;

            if (current_end == (void *)next_block) {

                /* Merge the blocks */
                current->size += HDR + next_block->size;

                /* Remove the next block from the linked list */
                current->next = next_block->next;

                /* Update tail if necessary */
                if (tail == next_block) {
                    tail = current;
                }

                continue;
            }
        }

        current = current->next;
    }
}
/* Release allocated memory */
void my_free(void *ptr)
{
    /* Ignore NULL pointers */
    if (ptr == NULL) {
        return;
    }

    /* Search for the block */
    block_t *block = head;

    while (block != NULL && payload(block) != ptr) {
        block = block->next;
    }

    /* Check for invalid pointers */
    if (block == NULL || block->magic != MAGIC) {
        const char message[] = "my_free: invalid pointer\n";
        write(STDERR_FILENO, message, sizeof(message) - 1);
        return;
    }

    /* Check for double free */
    if (block->free == 1) {
        const char message[] = "my_free: double free detected\n";
        write(STDERR_FILENO, message, sizeof(message) - 1);
        return;
    }

    /* Mark the block as free */
    block->free = 1;

    /* Merge adjacent free blocks */
    coalesce();
}