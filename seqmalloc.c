
#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include <sys/mman.h>
#include <errno.h>
#include <pthread.h>

#include "defines.h"
#include "seqmalloc.h"

/*
 * Begin seqmalloc compile time options
 */

// print debug-level messages for every malloc hook
#define SEQMALOC_DEBUG
// initial seqmalloc block size in bytes (default: 1 huge page = 2mb)
#define SEQMALLOC_SIZE_INITIAL      HUGEPAGE
// controls size increase of next block when current is exhausted
// default: 2, doubles size of next block
#define SEQMALLOC_SIZE_MULT         2


/*
 * End seqmalloc compile time options
 */

#ifdef SEQMALOC_DEBUG
#define PRINT_DEBUG(STR, ...) \
    fprintf(stdout, "%s:%d %s " STR "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);

#else
#define PRINT_DEBUG(str, ...)

#endif

#define PRINT_ERR(STR, ...) \
    fprintf(stderr, "%s:%d %s " STR "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)

/*
 * Begin seq_malloc global data
 */

struct block_t;

struct block_t
{
    struct block_t* prev_block;
    size_t size;
    char* max_ptr; // addresses >= max_ptr don't belong to block
    char* curr_ptr;
    char data[ ];
};

typedef struct block_t block_t;

// thread local data
// ptr to block in use for allocations
static _Thread_local block_t* curr_block;

// global data
// as threads exit, can't immediately free blocks
// need to store them and only free them at program exit
// needs to be an atomic pointer to a block_t
static block_t* _Atomic orphan_block_list = NULL;
static int initialized = 0;

/*
 * Begin seq_malloc functions
 */

void seq_malloc_initialize()
{
    if (initialized)
        return;

    initialized = 1;
    PRINT_DEBUG();
    if (atexit(seq_malloc_finalize) != 0)
        PRINT_ERR("cannot set exit function");
}

void seq_malloc_finalize()
{
    if (!initialized)
        return;

    initialized = 0;
    PRINT_DEBUG();

    block_t* prev = atomic_load_explicit(&orphan_block_list, memory_order_acquire);
    while (prev != NULL)
    {
        block_t* head = prev;
        prev = head->prev_block;

        // free block
        int ret = munmap(head, head->size);
        if (ret == -1)
            PRINT_ERR("munmap failed");
    }
}

void seq_malloc_thread_initialize()
{
    PRINT_DEBUG();
}

void seq_malloc_thread_finalize()
{
    PRINT_DEBUG();

    if (curr_block == NULL)
        return;

    // append blocks to global orphan list
    block_t* first_block = curr_block;
    while (first_block && first_block->prev_block)
        first_block = first_block->prev_block;

    block_t* last_head = atomic_load_explicit(&orphan_block_list, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(
                &orphan_block_list, &last_head, curr_block,
                memory_order_acq_rel, memory_order_acquire))
        ;


    if (first_block)
        first_block->prev_block = last_head;
}

bool alloc_next_block()
{
    size_t next_block_size = SEQMALLOC_SIZE_INITIAL;
    if (curr_block)
        next_block_size = curr_block->size * SEQMALLOC_SIZE_MULT;

    block_t* prev_block = curr_block;
 
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    curr_block = (block_t*)mmap(NULL, next_block_size, prot, flags, -1, 0);
    if (curr_block == MAP_FAILED)
    {
        PRINT_ERR("mmap failed");
        curr_block = NULL;
        return false;
    }

    curr_block->prev_block = prev_block;
    curr_block->size = next_block_size;
    curr_block->max_ptr = (char*)curr_block + curr_block->size;
    curr_block->curr_ptr = &curr_block->data[0];
    return true;
}

void* alloc(size_t size, size_t alignment)
{
    if (curr_block == NULL && !alloc_next_block())
        return NULL;

    do
    {
        char* ptr = curr_block->curr_ptr;
        ptr = ALIGN_ADDR(ptr, alignment);

        if (ptr + size < curr_block->max_ptr)
            return (void*)ptr;
    }
    while (alloc_next_block());

    return NULL;
}

void* seq_malloc(size_t size)
{
    return alloc(size, sizeof(void*));
}

void seq_free(void* ptr)
{
    // no-op
}

void* seq_calloc(size_t n, size_t size)
{
    if (size && n > SIZE_MAX / size)
        return NULL;

    return alloc(n * size, sizeof(void*));
}

void* seq_realloc(void* ptr, size_t size)
{
    void* new_ptr = seq_malloc(size);
    // @todo: need to copy data from ptr to new_ptr;
    seq_free(ptr); 
    return new_ptr;
}

int seq_posix_memalign(void** memptr, size_t alignment, size_t size)
{
    void* ptr = alloc(size, alignment);
    if (!ptr)
        return ENOMEM;

    *memptr = ptr;
    return 0;
}

void* seq_aligned_alloc(size_t alignment, size_t size)
{
    return alloc(size, alignment);
}

void* seq_valloc(size_t size)
{
    return alloc(size, PAGE);
}

void* seq_memalign(size_t alignment, size_t size)
{
    return alloc(size, alignment);
}

void* seq_pvalloc(size_t size)
{
    size = ALIGN_ADDR(size, PAGE);
    return alloc(size, PAGE);
}

/*
 * End seqmalloc functions.
 */

/*
 * Begin externals hooks to malloc
 */

// handle process init/exit hooks
static pthread_key_t destructor_key;

static void* thread_initializer(void*);
static void thread_finalizer(void*);

static SEQMALLOC_ATTR(constructor)
void initializer()
{
    static int is_initialized = 0;
    if (is_initialized == 0)
    {
        is_initialized = 1;
        pthread_key_create(&destructor_key, thread_finalizer);
    }

    seq_malloc_initialize();
    seq_malloc_thread_initialize();
}

static SEQMALLOC_ATTR(destructor)
void finalizer()
{
    seq_malloc_thread_finalize();
    seq_malloc_finalize();
}

// handle thread init/exit hooks
typedef struct
{
    void* (*real_start)(void*);
    void* real_arg;
} thread_starter_arg;

static void* thread_initializer(void* argptr)
{
    thread_starter_arg* arg = (thread_starter_arg*)argptr;
    void* (*real_start)(void*) = arg->real_start;
    void* real_arg = arg->real_arg;
    seq_malloc_thread_initialize();

    pthread_setspecific(destructor_key, (void*)1);
    return (*real_start)(real_arg);
}

static void thread_finalizer(void* value)
{
    seq_malloc_thread_finalize();
}

int pthread_create(pthread_t* thread,
                   pthread_attr_t const* attr,
                   void* (start_routine)(void*),
                   void* arg)
{
    static int (*pthread_create_fn)(pthread_t*,
                                    pthread_attr_t const*,
                                    void* (void*),
                                    void*) = NULL;
    if (pthread_create_fn == NULL)
        pthread_create_fn = dlsym(RTLD_NEXT, "pthread_create");

    // @todo: don't want to use malloc here
    // instead using a ringbuffer, which has limited storage
#define RING_BUFFER_SIZE 10000
    static uint32_t _Atomic ring_buffer_pos = 0;
    static thread_starter_arg ring_buffer[RING_BUFFER_SIZE];
    uint32_t buffer_pos = atomic_fetch_add_explicit(&ring_buffer_pos, 1, memory_order_relaxed);

    thread_starter_arg* starter_arg = &ring_buffer[buffer_pos];
    starter_arg->real_start = start_routine;
    starter_arg->real_arg = arg;
    seq_malloc_thread_initialize();
    return pthread_create_fn(thread, attr, thread_initializer, starter_arg);
}

