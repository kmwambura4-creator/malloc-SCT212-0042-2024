#define _DEFAULT_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "myalloc.h"

/* Structure representing one memory block */
typedef struct block {
    size_t size;
    struct block *next;
    int free;
    int magic;
} block_t;

/* Global pointers to manage the linked list */
static block_t *head = NULL;
static block_t *tail = NULL;

/* Total memory obtained from the operating system */
static size_t heap_bytes = 0;

/* Current allocation policy: First-Fit by default */
static fit_policy_t current_policy = FIT_FIRST;

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
    size_t chunk_size = 64 * 1024;

    if (need > chunk_size) {
        chunk_size = need;
    }

    chunk_size = align_up(chunk_size);

    void *current_break = sbrk(0);

    if (current_break == (void *)-1) {
        return NULL;
    }

    size_t padding =
        (16 - (((uintptr_t)current_break + HDR) % 16)) % 16;

    if (chunk_size > SIZE_MAX - HDR - padding) {
        return NULL;
    }

    size_t total_size = padding + HDR + chunk_size;

    void *memory = sbrk(total_size);

    if (memory == (void *)-1) {
        return NULL;
    }

    block_t *new_block =
        (block_t *)((char *)memory + padding);

    new_block->size = chunk_size;
    new_block->next = NULL;
    new_block->free = 1;
    new_block->magic = MAGIC;

    if (head == NULL) {
        head = new_block;
    } else {
        tail->next = new_block;
    }

    tail = new_block;

    heap_bytes += total_size;

    return new_block;
}

/* Find a suitable free block using the selected policy */
static block_t *find_fit(size_t need)
{
    block_t *current = head;
    block_t *best = NULL;

    while (current != NULL) {

        if (current->free == 1 && current->size >= need) {

            /* First-Fit: choose the first suitable block */
            if (current_policy == FIT_FIRST) {
                return current;
            }

            /* Best-Fit: choose the smallest suitable block */
            if (best == NULL || current->size < best->size) {
                best = current;
            }
        }

        current = current->next;
    }

    return best;
}

/* Select the allocation policy */
void set_policy(fit_policy_t p)
{
    if (p == FIT_FIRST || p == FIT_BEST) {
        current_policy = p;
    }
}

/* Split a block when it has enough extra space */
static void split_block(block_t *block, size_t need)
{
    if (block->size >= need &&
        block->size - need >= HDR + 16) {

        size_t remaining_size =
            block->size - need - HDR;

        block_t *new_block =
            (block_t *)((char *)payload(block) + need);

        new_block->size = remaining_size;
        new_block->free = 1;
        new_block->magic = MAGIC;
        new_block->next = block->next;

        block->next = new_block;

        if (tail == block) {
            tail = new_block;
        }

        block->size = need;
    }
}

/* Allocate memory using the selected strategy */
void *my_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    if (size > SIZE_MAX - 15) {
        return NULL;
    }

    size_t need = align_up(size);

    block_t *block = find_fit(need);

    if (block == NULL) {

        if (grow(need) == NULL) {
            return NULL;
        }

        block = find_fit(need);

        if (block == NULL) {
            return NULL;
        }
    }

    split_block(block, need);

    block->free = 0;

    return payload(block);
}

/* Merge physically adjacent free blocks */
static void coalesce(void)
{
    block_t *current = head;

    while (current != NULL && current->next != NULL) {

        block_t *next_block = current->next;

        if (current->free == 1 && next_block->free == 1) {

            void *current_end =
                (char *)payload(current) + current->size;

            if (current_end == (void *)next_block) {

                current->size += HDR + next_block->size;

                current->next = next_block->next;

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
    if (ptr == NULL) {
        return;
    }

    block_t *block = head;

    while (block != NULL && payload(block) != ptr) {
        block = block->next;
    }

    if (block == NULL || block->magic != MAGIC) {
        const char message[] = "my_free: invalid pointer\n";
        write(STDERR_FILENO, message, sizeof(message) - 1);
        return;
    }

    if (block->free == 1) {
        const char message[] = "my_free: double free detected\n";
        write(STDERR_FILENO, message, sizeof(message) - 1);
        return;
    }

    block->free = 1;

    coalesce();
}

/* Allocate memory and initialize it to zero */
void *my_calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        return NULL;
    }

    size_t total = nmemb * size;

    void *ptr = my_malloc(total);

    if (ptr == NULL) {
        return NULL;
    }

    memset(ptr, 0, total);

    return ptr;
}

/* Resize previously allocated memory */
void *my_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) {
        return my_malloc(size);
    }

    if (size == 0) {
        my_free(ptr);
        return NULL;
    }

    if (size > SIZE_MAX - 15) {
        return NULL;
    }

    block_t *block = head;

    while (block != NULL && payload(block) != ptr) {
        block = block->next;
    }

    if (block == NULL || block->magic != MAGIC ||
        block->free == 1) {
        return NULL;
    }

    size_t need = align_up(size);

    /* If the current block is already large enough */
    if (block->size >= need) {
        split_block(block, need);
        coalesce();
        return ptr;
    }

    /* Check whether the next block can be merged */
    block_t *next = block->next;

    if (next != NULL &&
        next->free == 1 &&
        (char *)payload(block) + block->size == (char *)next &&
        block->size <= SIZE_MAX - HDR - next->size &&
        block->size + HDR + next->size >= need) {

        block->size += HDR + next->size;
        block->next = next->next;

        if (tail == next) {
            tail = block;
        }

        split_block(block, need);

        return ptr;
    }

    /* Allocate a new block if expansion is necessary */
    void *new_ptr = my_malloc(size);

    if (new_ptr == NULL) {
        return NULL;
    }

    size_t copy_size =
        block->size < size ? block->size : size;

    memcpy(new_ptr, ptr, copy_size);

    my_free(ptr);

    return new_ptr;
}

/* Calculate heap statistics */
void heap_stats(heap_stats_t *out)
{
    if (out == NULL) {
        return;
    }

    out->heap_bytes = heap_bytes;
    out->used_bytes = 0;
    out->free_bytes = 0;
    out->blocks = 0;
    out->free_blocks = 0;
    out->largest_free = 0;

    block_t *current = head;

    while (current != NULL) {

        out->blocks++;

        if (current->free == 1) {

            out->free_bytes += current->size;
            out->free_blocks++;

            if (current->size > out->largest_free) {
                out->largest_free = current->size;
            }

        } else {
            out->used_bytes += current->size;
        }

        current = current->next;
    }
}

/* Display information about every block */
void heap_dump(void)
{
    block_t *current = head;
    size_t index = 0;
    char buffer[160];

    while (current != NULL) {

        int length = snprintf(
            buffer,
            sizeof(buffer),
            "[%2zu] %p size=%zu %s\n",
            index,
            (void *)current,
            current->size,
            current->free ? "FREE" : "USED"
        );

        if (length > 0) {
            size_t output_length =
                (size_t)length < sizeof(buffer)
                ? (size_t)length
                : sizeof(buffer) - 1;

            write(STDOUT_FILENO, buffer, output_length);
        }

        current = current->next;
        index++;
    }
}

/* Check whether the heap structure is consistent */
int heap_check(void)
{
    block_t *current = head;
    block_t *last = NULL;

    while (current != NULL) {

        /* Rule 1: Every block must have a valid magic number */
        if (current->magic != MAGIC) {
            return 0;
        }

        /* Rule 2: Every payload must be 16-byte aligned */
        if ((uintptr_t)payload(current) % 16 != 0) {
            return 0;
        }

        /* Payload sizes must also be aligned */
        if (current->size % 16 != 0) {
            return 0;
        }

        block_t *next = current->next;

        if (next != NULL) {

            /* Rule 3: Blocks must be in increasing address order */
            if ((uintptr_t)next <= (uintptr_t)current) {
                return 0;
            }

            /* Rule 4: Physically adjacent free blocks must be merged */
            void *current_end =
                (char *)payload(current) + current->size;

            if (current->free == 1 &&
                next->free == 1 &&
                current_end == (void *)next) {
                return 0;
            }
        }

        last = current;
        current = next;
    }

    /* Confirm that tail points to the final block */
    if (last != tail) {
        return 0;
    }

    return 1;
}