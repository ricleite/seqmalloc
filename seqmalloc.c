
#define _GNU_SOURCE
#include <dlfcn.h>

#include <assert.h>

#include <string.h>
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
#define SEQMALOC_DEBUG              0
// initial seqmalloc block size in bytes (default: 1 huge page = 2mb)
#define SEQMALLOC_SIZE_INITIAL      HUGEPAGE
// controls size increase of next block when current is exhausted
// default: 2, doubles size of next block
#define SEQMALLOC_SIZE_MULT         2


/*
 * End seqmalloc compile time options
 */

#if SEQMALOC_DEBUG
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

// thread data
// can't use standard _Thread_local, since it calls malloc
static pthread_key_t curr_block_key;

// global data
// as threads exit, can't immediately free blocks
// need to store them and only free them at program exit
// needs to be an atomic pointer to a block_t
static block_t* _Atomic orphan_block_list = NULL;
static int initialized = 0;

// thread-specific data helpers
block_t* get_curr_block()
{
    static bool init_tsd = false;

    if (!init_tsd)
    {
        init_tsd = true;
        pthread_key_create(&curr_block_key, NULL);
    }

    return (block_t*)pthread_getspecific(curr_block_key);
}

void set_curr_block(block_t* block)
{
    pthread_setspecific(curr_block_key, (void*)block);
}

// per-alloc data
struct alloc_data_t
{
    size_t size; // size in bytes
    char data[ ];
} SEQMALLOC_ATTR(aligned(MIN_ALIGN));

typedef struct alloc_data_t alloc_data_t;

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

    setbuf(stdout, NULL);
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
    PRINT_DEBUG("%p", (void*)pthread_self());
}

void seq_malloc_thread_finalize()
{
    PRINT_DEBUG("%p", (void*)pthread_self());

    // append blocks to global orphan list
    block_t* first_block = get_curr_block();
    if (first_block == NULL)
        return; // no blocks to append

    while (first_block && first_block->prev_block)
        first_block = first_block->prev_block;

    block_t* last_head = atomic_load_explicit(&orphan_block_list, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(
                &orphan_block_list, &last_head, get_curr_block(),
                memory_order_acq_rel, memory_order_acquire));

    if (first_block)
        first_block->prev_block = last_head;
}

bool alloc_next_block()
{
    size_t next_block_size = SEQMALLOC_SIZE_INITIAL;

    block_t* prev_block = get_curr_block();
    if (prev_block)
        next_block_size = prev_block->size * SEQMALLOC_SIZE_MULT;
 
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    block_t* next_block = (block_t*)mmap(NULL, next_block_size, prot, flags, -1, 0);
    if (next_block == MAP_FAILED)
    {
        PRINT_ERR("mmap failed");
        set_curr_block(NULL);
        return false;
    }

    next_block->prev_block = prev_block;
    next_block->size = next_block_size;
    next_block->max_ptr = (char*)next_block + next_block->size;
    next_block->curr_ptr = &next_block->data[0];

    set_curr_block(next_block);
    return true;
}

// retrieve alloc_data_t from alloc'd ptr
alloc_data_t* ptr2data(void* ptr)
{
    // | alloc_data_t | alloc |
    // alloc data is located on the words before alloc
    // no issues with alignment, since ptr has at least MIN_ALIGN 
    return &((alloc_data_t*)ptr)[-1];
}

void* alloc(size_t size, size_t alignment)
{
    PRINT_DEBUG("thread: %p", (void*)pthread_self());

    do
    {
        block_t* curr_block = get_curr_block();
        if (curr_block == NULL)
            continue;

        // reserve some space for alloc_data_t
        char* ptr = curr_block->curr_ptr + sizeof(alloc_data_t);
        ptr = ALIGN_ADDR(ptr, alignment);

        if (ptr + size < curr_block->max_ptr)
        {
            alloc_data_t* data = ptr2data(ptr);
            data->size = size;

            curr_block->curr_ptr = ptr + size;
            return (void*)ptr;
        }
    }
    while (alloc_next_block());

    return NULL;
}

void* seq_malloc(size_t size)
{
    PRINT_DEBUG();

    return alloc(size, MIN_ALIGN);
}

void seq_free(void* ptr)
{
    PRINT_DEBUG();

    // no-op
}

void* seq_calloc(size_t n, size_t size)
{
    PRINT_DEBUG();

    if (size && n > SIZE_MAX / size)
        return NULL;

    return alloc(n * size, sizeof(void*));
}

void* seq_realloc(void* ptr, size_t size)
{
    PRINT_DEBUG();

    void* new_ptr = seq_malloc(size);

    if (ptr)
    {
        alloc_data_t* data = ptr2data(ptr);
        memcpy(new_ptr, ptr, data->size);
    }

    seq_free(ptr); 
    return new_ptr;
}

size_t seq_malloc_usable_size(void* ptr)
{
    if (ptr == NULL)
        return 0;

    alloc_data_t* data = ptr2data(ptr);
    return data->size;
}

int seq_posix_memalign(void** memptr, size_t alignment, size_t size)
{
    PRINT_DEBUG();

    void* ptr = alloc(size, alignment);
    if (!ptr)
        return ENOMEM;

    *memptr = ptr;
    return 0;
}

void* seq_aligned_alloc(size_t alignment, size_t size)
{
    PRINT_DEBUG();

    return alloc(size, alignment);
}

void* seq_valloc(size_t size)
{
    PRINT_DEBUG();

    return alloc(size, PAGE);
}

void* seq_memalign(size_t alignment, size_t size)
{
    PRINT_DEBUG();

    return alloc(size, alignment);
}

void* seq_pvalloc(size_t size)
{
    PRINT_DEBUG();

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
    return pthread_create_fn(thread, attr, thread_initializer, starter_arg);
}

