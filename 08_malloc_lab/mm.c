/*
 ******************************************************************************
 *                                   mm.c                                     *
 *           64-bit struct-based implicit free list memory allocator          *
 *                  15-213: Introduction to Computer Systems                  *
 *                                                                            *
 *  ************************************************************************  *
 *                                                                            *
 *  ************************************************************************  *
 *  ** ADVICE FOR STUDENTS. **                                                *
 *  Step 0: Please read the writeup!                                          *
 *  Step 1: Write your heap checker. Write. Heap. checker.                    *
 *  Step 2: Place your contracts / debugging assert statements.               *
 *  Good luck, and have fun!                                                  *
 *                                                                            *
 ******************************************************************************
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* Do not change the following! */

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 * If DEBUG is defined (such as when running mdriver-dbg), these macros
 * are enabled. You can use them to print debugging output and to check
 * contracts only in debug mode.
 *
 * Only debugging macros with names beginning "dbg_" are allowed.
 * You may not define any other macros having arguments.
 */
#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, no code gets generated for these */
/* The sizeof() hack is used to avoid "unused variable" warnings */
#define dbg_printf(...) (sizeof(__VA_ARGS__), -1)
#define dbg_requires(expr) (sizeof(expr), 1)
#define dbg_assert(expr) (sizeof(expr), 1)
#define dbg_ensures(expr) (sizeof(expr), 1)
#define dbg_printheap(...) ((void)sizeof(__VA_ARGS__))
#endif

/* Basic constants */

typedef uint64_t word_t;

// Word and header size (bytes)
static const size_t wsize = sizeof(word_t);

// Double word size (bytes)
static const size_t dsize = 2 * wsize;

// Minimum block size (bytes)
static const size_t min_block_size = 2 * dsize;

// chuncksize is how much memory we request from sbrk
// (Must be divisible by dsize)
static const size_t chunksize = (1 << 14);

// a mask to extract the allocated or free bit
static const word_t alloc_mask = 0x1;

// a mask to hide the extra information bits and extract size
static const word_t size_mask = ~(word_t)0xF;

typedef union {
  char *char_ptr;
  word_t *word_ptr;
  long addr;
} mem;

/* Represents the header and payload of one block in the heap */
typedef struct block {
  /* Header contains size + allocation flag */
  word_t header;

  /*
   * TODO: feel free to delete this comment once you've read it carefully.
   * We don't know what the size of the payload will be, so we will declare
   * it as a zero-length array, which is a GCC compiler extension. This will
   * allow us to obtain a pointer to the start of the payload.
   *
   * WARNING: A zero-length array must be the last element in a struct, so
   * there should not be any struct fields after it. For this lab, we will
   * allow you to include a zero-length array in a union, as long as the
   * union is the last field in its containing struct. However, this is
   * compiler-specific behavior and should be avoided in general.
   *
   * WARNING: DO NOT cast this pointer to/from other types! Instead, you
   * should use a union to alias this zero-length array with another struct,
   * in order to store additional types of data in the payload memory.
   */
  char payload[0];

  /*
   * TODO: delete or replace this comment once you've thought about it.
   * Why can't we declare the block footer here as part of the struct?
   * Why do we even have footers -- will the code work fine without them?
   * which functions actually use the data contained in footers?
   */
} block_t;

typedef struct {
  block_t *prev;
  block_t *next;
} link_t;

typedef union {
  link_t *link;
  word_t *ptr;
} node_t;

/* Global variables */

// Pointer to first block
static block_t *heap_start = NULL;
// Free block start
static block_t *first_free_node = NULL;
static block_t *last_free_node = NULL;

/* Function prototypes for internal helper routines */

bool mm_checkheap(int lineno);

static block_t *extend_heap(size_t size);
static block_t *find_fit(size_t asize);
static block_t *coalesce_block(block_t *block);
static bool split_block(block_t *block, size_t asize);

static size_t max(size_t x, size_t y);
static size_t round_up(size_t size, size_t n);
static word_t pack(size_t size, bool alloc);

static size_t extract_size(word_t header);
static size_t get_size(block_t *block);
static size_t get_payload_size(block_t *block);

static bool extract_alloc(word_t header);
static bool get_alloc(block_t *block);

static void write_header(block_t *block, size_t size, bool alloc);
static void write_footer(block_t *block, size_t size, bool alloc);

static block_t *payload_to_header(void *bp);
static void *header_to_payload(block_t *block);
static word_t *header_to_footer(block_t *block);

static block_t *find_next(block_t *block);
static word_t *find_prev_footer(block_t *block);
static block_t *find_prev(block_t *block);

/* my added functions*/

static void set_last_node(block_t *block);
static block_t *find_next_free_node(block_t *block);
static block_t *find_prev_free_node(block_t *block);
static link_t *header_to_link(block_t *block);
static void insert_first_node(block_t *block);
static void print_heap();
static void initialize_links(block_t *block);

/*
 * <What does this function do?>
 *      initilizes an empty heap then extends it with a chunck
 * <What are the function's arguments?>
 *      nothing
 * <What is the function's return value?>
 *      whether the initialization was successful
 * <Are there any preconditions or postconditions?>
 *      -
 */
bool mm_init(void) {
  // Create the initial empty heap
  word_t *start = (word_t *)(mem_sbrk(2 * wsize));

  if (start == (void *)-1) {
    return false;
  }

  /*
   * Think about why we need a heap prologue and epilogue. Why do
   * they correspond to a block footer and header respectively?
   *    will making searching the linked list a lot easier
   */

  start[0] = pack(0, true); // Heap prologue (block footer)
  start[1] = pack(0, true); // Heap epilogue (block header)

  first_free_node = NULL;
  last_free_node = NULL;

  // Heap starts with first "block header", currently the epilogue
  heap_start = (block_t *)&(start[1]);
  // Extend the empty heap with a free block of chunksize bytes
  if (extend_heap(chunksize) == NULL) {
    return false;
  }

  first_free_node = heap_start;
  last_free_node = heap_start;
  initialize_links(first_free_node);
  return true;
}

/*
 * <What does this function do?>
 *
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
void *malloc(size_t size) {
  dbg_requires(mm_checkheap(__LINE__));

  size_t asize;      // Adjusted block size
  size_t extendsize; // Amount to extend heap if no fit is found
  block_t *block;
  void *bp = NULL;

  if (heap_start == NULL) // Initialize heap if it isn't initialized
  {
    mm_init();
  }

  if (size == 0) // Ignore spurious request
  {
    dbg_ensures(mm_checkheap(__LINE__));
    return bp;
  }

  // Adjust block size to include overhead and to meet alignment requirements
  asize = round_up(size + dsize, dsize);

  // Search the free list for a fit
  block = find_fit(asize);

  // If no fit is found, request more memory, and then and place the block
  if (block == NULL) {
    // Always request at least chunksize
    extendsize = max(asize, chunksize);
    block = extend_heap(extendsize);
    if (block == NULL) // extend_heap returns an error
    {
      return bp;
    }
  }

  // The block should be marked as free
  dbg_assert(!get_alloc(block));

  // Mark block as allocated
  size_t block_size = get_size(block);
  write_header(block, block_size, true);
  write_footer(block, block_size, true);

  // Try to split the block if too large
  bool splitted = split_block(block, asize);

  block_t *prev_free_node = find_prev_free_node(block);
  block_t *next_free_node = find_next_free_node(block);
  link_t *curr_free_link = header_to_link(block);

  if (splitted) {
    // TODO-lab do something here
    block_t *new_free_node = find_next(block);
    initialize_links(new_free_node);
    link_t *new_free_link = header_to_link(new_free_node);

    if (prev_free_node != NULL) {
      link_t *prev_free_link = header_to_link(prev_free_node);
      prev_free_link->next = new_free_node;
      new_free_link->prev = prev_free_node;
    }

    if (next_free_node != NULL) {
      link_t *next_free_link = header_to_link(next_free_node);
      next_free_link->prev = new_free_node;
      new_free_link->next = next_free_node;
    }

    if (first_free_node == block) {
      first_free_node = new_free_node;
    }

    if (last_free_node == block) {
      last_free_node = new_free_node;
    }

  } else {
    // make prev point to next
    if (prev_free_node != NULL) {
      link_t *prev_free_link = header_to_link(prev_free_node);
      prev_free_link->next = curr_free_link->next;
    }

    if (next_free_node != NULL) {
      link_t *next_free_link = header_to_link(next_free_node);
      next_free_link->prev = curr_free_link->prev;
    }

    if (first_free_node == block) {
      first_free_node = curr_free_link->next;
    }

    if (last_free_node == block) {
      last_free_node = curr_free_link->prev;
    }
  }

  bp = header_to_payload(block);

  dbg_ensures(mm_checkheap(__LINE__));
  // dbg_printheap(size, 1, block);
  return bp;
}

/*
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
void free(void *bp) {
  dbg_requires(mm_checkheap(__LINE__));

  if (bp == NULL) {
    return;
  }

  block_t *block = payload_to_header(bp);
  size_t size = get_size(block);

  // The block should be marked as allocated
  dbg_assert(get_alloc(block));

  // Mark the block as free
  write_header(block, size, false);
  write_footer(block, size, false);

  initialize_links(block);

  // dbg_printheap(size, 2, block);
  // Try to coalesce the block with its neighbors
  block = coalesce_block(block);

  // dbg_printheap(get_size(block), 3, block);
  dbg_ensures(mm_checkheap(__LINE__));
}

/*
 * <What does this function do?>
 *<What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
void *realloc(void *ptr, size_t size) {
  block_t *block = payload_to_header(ptr);
  size_t copysize;
  void *newptr;

  // If size == 0, then free block and return NULL
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  // If ptr is NULL, then equivalent to malloc
  if (ptr == NULL) {
    return malloc(size);
  }

  // Otherwise, proceed with reallocation
  newptr = malloc(size);

  // If malloc fails, the original block is left untouched
  if (newptr == NULL) {
    return NULL;
  }

  // Copy the old data
  copysize = get_payload_size(block); // gets size of old payload
  if (size < copysize) {
    copysize = size;
  }
  memcpy(newptr, ptr, copysize);

  // Free the old block
  free(ptr);

  return newptr;
}

/*
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
void *calloc(size_t elements, size_t size) {
  void *bp;
  size_t asize = elements * size;

  if (asize / elements != size) {
    // Multiplication overflowed
    return NULL;
  }

  bp = malloc(asize);
  if (bp == NULL) {
    return NULL;
  }

  // Initialize all bits to 0
  memset(bp, 0, asize);

  return bp;
}

/******** The remaining content below are helper and debug routines ********/

/*
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
static block_t *extend_heap(size_t size) {
  void *bp;

  // Allocate an even number of words to maintain alignment
  size = round_up(size, dsize);
  if ((bp = mem_sbrk(size)) == (void *)-1) {
    return NULL;
  }

  /*
   * Think about what bp represents. Why do we write the new block
   * starting one word BEFORE bp, but with the same size that we
   * originally requested?
   *    bp is the address of the first byte of the newly allocated memory,
   *    we do so to override the epilogue and move it the end of the heap.
   */

  // Initialize free block header/footer
  block_t *block = payload_to_header(bp);
  write_header(block, size, false);
  write_footer(block, size, false);

  initialize_links(block);

  // Create new epilogue header
  block_t *block_next = find_next(block);
  write_header(block_next, 0, true);

  // Coalesce in case the previous block was free
  block = coalesce_block(block);

  // TODO-lab: Update the free list
  link_t *free_link = header_to_link(block);
  if (first_free_node == NULL) {
    first_free_node = block;
    free_link->prev = NULL;
  }

  if (last_free_node == NULL) {
    last_free_node = block;
    free_link->next = NULL;
  }

  if (last_free_node != block) {
    set_last_node(block);
  }

  return block;
}

/*
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
static block_t *coalesce_block(block_t *block) {
  dbg_requires(!get_alloc(block));

  size_t size = get_size(block);

  /*
   * Think about how we find the prev and next blocks. What information
   * do we need to have about the heap in order to do this? Why doesn't
   * "bool prev_alloc = get_alloc(block_prev)" work properly?
   */

  // TODO-lab: remember special case when coalescing first or last free node

  block_t *block_next = find_next(block);
  block_t *block_prev = find_prev(block);

  bool prev_alloc = extract_alloc(*find_prev_footer(block));
  bool next_alloc = get_alloc(block_next);

  link_t *curr_free_link = header_to_link(block);

  // If can not coalesce, then add just this block to the free list
  if (prev_alloc && next_alloc) // Case 1
  {
    if (first_free_node != NULL && block < first_free_node) {
      insert_first_node(block);
    } else if (last_free_node != NULL && block > last_free_node) {
      set_last_node(block);
    } else {
      block_t *block_ptr;
      for (block_ptr = first_free_node; block_ptr != NULL;
           block_ptr = find_next_free_node(block_ptr)) {
        link_t *ptr_link = header_to_link(block_ptr);
        if (ptr_link->next > block) {
          block_t *next_free_node = ptr_link->next;

          ptr_link->next = block;
          curr_free_link->prev = block_ptr;

          if (next_free_node != NULL) {
            link_t *next_free_link = header_to_link(next_free_node);
            next_free_link->prev = block;
          }

          curr_free_link->next = next_free_node;
          break;
        }
      }
    }
  }

  else if (prev_alloc && !next_alloc) // Case 2
  {
    size += get_size(block_next);
    write_header(block, size, false);
    write_footer(block, size, false);

    link_t *adjacent_free_link = header_to_link(block_next);
    if (adjacent_free_link->prev != NULL) {
      link_t *prev_free_link = header_to_link(adjacent_free_link->prev);
      prev_free_link->next = block;
    }

    if (adjacent_free_link->next != NULL) {
      link_t *next_free_link = header_to_link(adjacent_free_link->next);
      next_free_link->prev = block;
    }

    curr_free_link->next = adjacent_free_link->next;
    curr_free_link->prev = adjacent_free_link->prev;

    if (block_next == last_free_node) {
      last_free_node = block;
    }

    if (block_next == first_free_node) {
      first_free_node = block;
    }
  }

  else if (!prev_alloc && next_alloc) // Case 3
  {
    size += get_size(block_prev);
    write_header(block_prev, size, false);
    write_footer(block_prev, size, false);
    block = block_prev;
  }

  else // Case 4
  {
    size += get_size(block_next) + get_size(block_prev);
    write_header(block_prev, size, false);
    write_footer(block_prev, size, false);

    link_t *link_next = header_to_link(block_next);
    link_t *link_prev = header_to_link(block_prev);

    link_prev->next = link_next->next;
    if (link_next->next != NULL) {
      header_to_link(link_next->next)->prev = block_prev;
    }

    block = block_prev;

    if (block_next == last_free_node) {
      last_free_node = block;
    }

    if (block_next == first_free_node) {
      first_free_node = block;
    }
  }

  dbg_ensures(!get_alloc(block));

  return block;
}

/*
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
static bool split_block(block_t *block, size_t asize) {
  dbg_requires(get_alloc(block));
  // TODO: special case when splitting first or last free node

  size_t block_size = get_size(block);

  if ((block_size - asize) >= min_block_size) {
    block_t *block_next;
    write_header(block, asize, true);
    write_footer(block, asize, true);

    block_next = find_next(block);
    write_header(block_next, block_size - asize, false);
    write_footer(block_next, block_size - asize, false);

    initialize_links(block_next);

    dbg_ensures(get_alloc(block));
    return true;
  }

  dbg_ensures(get_alloc(block));
  return false;
}

/*
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
static block_t *find_fit(size_t asize) {
  block_t *block;

  for (block = first_free_node; block != NULL;
       block = find_next_free_node(block)) {

    if (!(get_alloc(block)) && (asize <= get_size(block))) {
      return block;
    }
  }
  return NULL; // no fit found
}

/*
 * <What does this function do?>
 * <What are the function's arguments?>
 * <What is the function's return value?>
 * <Are there any preconditions or postconditions?>
 */
bool mm_checkheap(int line) {
  /*
   * TODO: Delete this comment!
   *
   * You will need to write the heap checker yourself.
   * Please keep modularity in mind when you're writing the heap checker!
   *
   * As a filler: one guacamole is equal to 6.02214086 x 10**23 guacas.
   * One might even call it...  the avocado's number.
   *
   * Internal use only: If you mix guacamole on your bibimbap,
   * do you eat it with a pair of chopsticks, or with a spoon?
   */
  word_t *prologue = find_prev_footer(heap_start);
  block_t *header = heap_start;
  word_t *footer, *prev_footer;
  mem location;
  size_t header_size, footer_size;
  bool header_a, footer_a, prev_footer_a;
  // Prologue should be allocated
  location.word_ptr = prologue;
  if (!extract_alloc(*prologue) || extract_size(*prologue) != 0 ||
      location.addr % dsize != 0) {
    printf("\n(mm_checkheap) failed because prologue is not allocated, or has "
           "size > 0 or addr MOD dsize != 0\n");
    dbg_printheap(0, 0, 0);
    return false;
  }
  while ((header_size = get_size(header)) > 0) {
    // payload should be aligned to 16
    location.char_ptr = header_to_payload(header);
    if (location.addr % 16 != 0) {
      printf("\n(mm_checkheap) failed because location.addr MOD 16 != 0\n");
      dbg_printheap(0, 0, 0);
      return false;
    }
    // Check if the header size matches with footer size
    footer = header_to_footer(header);
    footer_size = extract_size(*footer);
    if (header_size != footer_size) {
      printf("\n(mm_checkheap) failed because @%p header_size != footer_size\n",
             (void *)header);
      dbg_printheap(0, 0, 0);
      return false;
    }
    // Check if allocated flags are same
    header_a = get_alloc(header);
    footer_a = extract_alloc(*footer);
    if (header_a != footer_a) {
      printf("\n(mm_checkheap) failed because allocation flags not the same in "
             "header and footer\n");
      dbg_printheap(0, 0, 0);
      return false;
    }
    // Check no two consecutive free blocks
    prev_footer = find_prev_footer(header);
    prev_footer_a = extract_alloc(*prev_footer);
    if (!prev_footer_a && !header_a) {
      printf("\n(mm_checkheap) failed because two consecutive free blocks "
             "found\n");
      dbg_printheap(0, 0, 0);
      return false;
    }
    header = find_next(header);
  }
  // Epilogue should remain allocated
  if (!get_alloc(header) || get_size(header) != 0) {
    printf("\n(mm_checkheap) failed because epilogue is not allocated, or has "
           "size > 0\n");
    dbg_printheap(0, 0, 0);
    return false;
  }

  // First node should have prev = NULL
  if (first_free_node != NULL &&
      header_to_link(first_free_node)->prev != NULL) {
    printf(
        "\n(mm_checkheap) failed because @%p first_free_node->prev != NULL \n",
        first_free_node);
    dbg_printheap(0, 0, 0);
    return false;
  }

  // Last node should have next = NULL
  if (last_free_node != NULL && header_to_link(last_free_node)->next != NULL) {
    printf(
        "\n(mm_checkheap) failed because @%p last_free_node->next != NULL \n",
        last_free_node);
    dbg_printheap(0, 0, 0);
    return false;
  }

  // Every free list node should have its prev and next correct
  for (header = first_free_node; header != NULL;
       header = find_next_free_node(header)) {
    link_t *header_link = header_to_link(header);
    if (header_link->next != NULL) {
      block_t *next_node = header_link->next;
      link_t *next_link = header_to_link(next_node);
      if (next_link->prev != header) {
        printf("\n(mm_checkheap) failed because there is mis-aligned prev and "
               "next between @%p and @%p\n",
               header, next_link->prev);
        dbg_printheap(0, 0, 0);
        return false;
      }
    }
  }
  return true;
}

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details within your header comments for the functions above!     *
 *                                                                           *
 *                                                                           *
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a de ba c1 e1 52 13 0a               *
 *                                                                           *
 *****************************************************************************
 */

/*
 * max: returns x if x > y, and y otherwise.
 */
static size_t max(size_t x, size_t y) { return (x > y) ? x : y; }

/*
 * round_up: Rounds size up to next multiple of n
 */
static size_t round_up(size_t size, size_t n) {
  return n * ((size + (n - 1)) / n);
}

/*
 * pack: returns a header reflecting a specified size and its alloc status.
 *       If the block is allocated, the lowest bit is set to 1, and 0 otherwise.
 */
static word_t pack(size_t size, bool alloc) {
  return alloc ? (size | alloc_mask) : size;
}

/*
 * extract_size: returns the size of a given header value based on the header
 *               specification above.
 */
static size_t extract_size(word_t word) { return (word & size_mask); }

/*
 * get_size: returns the size of a given block by clearing the lowest 4 bits
 *           (as the heap is 16-byte aligned).
 */
static size_t get_size(block_t *block) { return extract_size(block->header); }

/*
 * get_payload_size: returns the payload size of a given block, equal to
 *                   the entire block size minus the header and footer sizes.
 */
static word_t get_payload_size(block_t *block) {
  size_t asize = get_size(block);
  return asize - dsize;
}

/*
 * extract_alloc: returns the allocation status of a given header value based
 *                on the header specification above.
 */
static bool extract_alloc(word_t word) { return (bool)(word & alloc_mask); }

/*
 * get_alloc: returns true when the block is allocated based on the
 *            block header's lowest bit, and false otherwise.
 */
static bool get_alloc(block_t *block) { return extract_alloc(block->header); }

/*
 * write_header: given a block and its size and allocation status,
 *               writes an appropriate value to the block header.
 * TODO: Are there any preconditions or postconditions?
 */
static void write_header(block_t *block, size_t size, bool alloc) {
  dbg_requires(block != NULL);
  block->header = pack(size, alloc);
}

/*
 * write_footer: given a block and its size and allocation status,
 *               writes an appropriate value to the block footer by first
 *               computing the position of the footer.
 * TODO: Are there any preconditions or postconditions?
 */
static void write_footer(block_t *block, size_t size, bool alloc) {
  dbg_requires(block != NULL);
  dbg_requires(get_size(block) == size && size > 0);
  word_t *footerp = header_to_footer(block);
  *footerp = pack(size, alloc);
}

/*
 * find_next: returns the next consecutive block on the heap by adding the
 *            size of the block.
 */
static block_t *find_next(block_t *block) {
  dbg_requires(block != NULL);
  dbg_requires(get_size(block) != 0);
  return (block_t *)((char *)block + get_size(block));
}

/*
 * find_prev_footer: returns the footer of the previous block.
 */
static word_t *find_prev_footer(block_t *block) {
  // Compute previous footer position as one word before the header
  return &(block->header) - 1;
}

/*
 * find_prev: returns the previous block position by checking the previous
 *            block's footer and calculating the start of the previous block
 *            based on its size.
 */
static block_t *find_prev(block_t *block) {
  dbg_requires(block != NULL);
  dbg_requires(get_size(block) != 0);
  word_t *footerp = find_prev_footer(block);
  size_t size = extract_size(*footerp);
  return (block_t *)((char *)block - size);
}

/*
 * payload_to_header: given a payload pointer, returns a pointer to the
 *                    corresponding block.
 */
static block_t *payload_to_header(void *bp) {
  return (block_t *)((char *)bp - offsetof(block_t, payload));
}

/*
 * header_to_payload: given a block pointer, returns a pointer to the
 *                    corresponding payload.
 */
static void *header_to_payload(block_t *block) {
  return (void *)(block->payload);
}

/*
 * header_to_footer: given a block pointer, returns a pointer to the
 *                   corresponding footer.
 */
static word_t *header_to_footer(block_t *block) {
  return (word_t *)(block->payload + get_size(block) - dsize);
}

static link_t *header_to_link(block_t *block) {
  node_t node;
  node.ptr = header_to_payload(block);
  return node.link;
}
/*
 * In case we extended the heap, malloc'd or splitted the last free node, we
 * need to update the pointer to the last free node
 */
static void set_last_node(block_t *block) {
  // TODO-lab update assertions
  link_t *current_node_link = header_to_link(block);
  current_node_link->prev = last_free_node;
  current_node_link->next = NULL;

  link_t *last_node_link = header_to_link(last_free_node);
  last_node_link->next = block;

  last_free_node = block;
}

static void insert_first_node(block_t *block) {
  // TODO-lab update assertions
  link_t *current_node_link = header_to_link(block);
  current_node_link->next = first_free_node;
  current_node_link->prev = NULL;

  link_t *first_node_link = header_to_link(first_free_node);
  first_node_link->prev = block;

  first_free_node = block;
}

static block_t *find_next_free_node(block_t *block) {
  // TODO-lab update assertions
  link_t *node = header_to_link(block);
  return node->next;
}

static block_t *find_prev_free_node(block_t *block) {
  // TODO-lab update assertions
  link_t *node = header_to_link(block);
  return node->prev;
}

static void print_heap(size_t size, int op, void *block_header) {
  char *action = "allocating";
  if (op == 2)
    action = "freeing";
  if (op == 3)
    action = "coalescing";

  if (size > 0) {
    printf("\nheap after %s %zu (0x%zx) bytes @%p\n", action, size, size,
           block_header);
  }

  printf("first_free_node = %p\n", (void *)first_free_node);
  printf("========\n");

  block_t *ptr;
  for (ptr = heap_start; get_size(ptr) != 0; ptr = find_next(ptr)) {
    printf("%p: %lx\n", (void *)ptr, *(word_t *)ptr);

    printf("%p: %lx\n", header_to_payload(ptr),
           *(word_t *)header_to_payload(ptr));
    if (get_alloc(ptr) == false) {
      link_t *link = header_to_link(ptr);
      // printf("%p: %lx\n", link->next, *(word_t *) link->next);
      printf("%p: %lx\n", (void *)&link->next, (word_t)link->next);
    }
    printf("%p: %lx\n", header_to_footer(ptr),
           *(word_t *)header_to_footer(ptr));
    printf("========\n");
  }
  printf("last_free_node = %p\n", (void *)last_free_node);
  printf("\n");
}

static void initialize_links(block_t *block) {
  link_t *block_link = header_to_link(block);
  block_link->prev = NULL;
  block_link->next = NULL;
}
