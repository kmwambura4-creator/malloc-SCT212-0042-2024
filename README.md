Custom Memory Allocator (myalloc)
Name:kelvin mwangi
Student ID: SCT212-00422024

Build and Run
make          # builds and runs test_myalloc
make bench    # builds and runs the Task 8 benchmark
make clean    # removes compiled binaries and object files
Tasks Checklist
Task	Description	Status
Task 1	Block header and helpers	Complete
Task 2	Growing the heap with sbrk	Complete
Task 3	my_malloc with first-fit	Complete
Task 4	Splitting blocks	Complete
Task 5	my_free and coalescing	Complete
Task 6	my_calloc and my_realloc	Complete
Task 7	Best-fit, statistics, heap dump, and heap check	Complete
Task 8	Experiment and report	Complete
Test Results
  PASS  no data corruption during 20000 operations
  PASS  everything freed
  PASS  fully coalesced (few free blocks remain)
  PASS  heap consistent

Result: 29 passed, 0 failed
Design Explanation
The allocator manages heap memory obtained via sbrk() as a singly linked list of blocks ordered by increasing memory address. Each block begins with a block_t header containing size (aligned payload bytes), next (pointer to the next block), free (status flag), and magic (a constant 0x5A110C42 used to detect invalid pointers). The header size is rounded up to a 16-byte multiple (HDR = 32 bytes on 64-bit Linux), and every requested payload size is rounded up to a multiple of 16, guaranteeing that every returned payload pointer is 16-byte aligned.

When my_malloc() cannot find a suitable free block under the active policy (FIT_FIRST or FIT_BEST), grow() requests at least 64 KB from the OS, adding initial padding if sbrk(0) is not 16-byte aligned, and appends the new free block to the tail. If a chosen block is larger than needed by at least HDR + 16 bytes, split_block() carves off the remainder as a new free block immediately following it.

On my_free(), the pointer's alignment, magic number, and free flag are validated before marking the block free. coalesce() then walks the list and merges any two consecutive free blocks only if they are physically adjacent in memory ((char *)a + HDR + a->size == (char *)b), preventing corruption across gaps created by libc's own sbrk() calls.

Questions
Why must the header size be a multiple of 16 as well as the payload sizes?
Every payload immediately follows its header ((char *)block + HDR). If the block starts on a 16-byte boundary, the payload will only be 16-byte aligned if HDR is also a multiple of 16. Keeping both the header and every payload size as multiples of 16 ensures that all subsequent split or adjacent blocks remain 16-byte aligned as well.

Why can printf() inside my_malloc() cause infinite recursion?
Standard I/O functions like printf() may internally call malloc() to allocate internal output buffers. If my_malloc() were interposing on malloc() (or calling functions that re-enter the allocator), calling printf() inside it would trigger another allocation, causing infinite recursion and a stack overflow.

Your my_free() is O(n). Which extra field would let you find a block's previous neighbor in O(1), and what is the name of the technique?
Adding a footer (which mirrors the block's size and free status at the very end of the block) allows my_free() to inspect the previous physical block in O(1) time by stepping backward from the current header. This technique is known as boundary tags (or using a doubly linked list with a prev pointer).

Explain the difference between internal and external fragmentation, and name one place in your allocator where each one comes from.
Internal fragmentation is wasted space inside an allocated block; in our allocator, it comes from rounding requests up to 16-byte multiples or refusing to split a block when the remainder is smaller than HDR + 16 bytes. External fragmentation is wasted space between allocated blocks when enough total free memory exists but no single contiguous free block is large enough; in our allocator, it comes from repeated variable-sized allocations and frees leaving scattered small holes.

Why does sbrk() memory never get returned to the OS in your allocator? Describe how you could give back memory at the end of the heap.
sbrk() only moves a single contiguous program break, so our allocator keeps freed blocks on the free list for reuse rather than shrinking the break. To return memory to the OS, we could check after coalescing whether tail is free, physically ends at the current sbrk(0), and exceeds a threshold (e.g., 64 KB); if so, we could unlink tail and call sbrk(-((intptr_t)(HDR + tail->size))) to lower the program break.

Two blocks are consecutive in your list but not adjacent in memory. How did this happen, and what does your coalescing code do about it?
This happens because the C library's own malloc() (used by printf() in the test harness) also calls sbrk() between our grow() calls, leaving a gap of libc-managed memory between two of our chunks. Our coalescing code checks exact physical adjacency ((char *)a + HDR + a->size == (char *)b) and refuses to merge blocks across a gap.

Known Limitations
Uses a single singly linked list, so allocation and coalescing are O(n).
Not thread-safe (no mutexes guarding global list pointers).
Memory obtained via sbrk() is never returned to the OS.
Sources and AI-Tool Declaration
Assignment specification and starter pack (myalloc.h, test_myalloc.c, Makefile).
Linux man pages (sbrk(2), write(2), clock_gettime(2)).
AI assistance (Gemini) was used to help structure, debug, and review the allocator implementation, benchmark, and documentation.
