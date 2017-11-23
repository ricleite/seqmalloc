
#ifndef __SEQMALLOC_H__
#define __SEQMALLOC_H__

#define SEQMALLOC_ATTR(s) __attribute__((s))
#define SEQMALLOC_ALLOC_SIZE(s) SEQMALLOC_ATTR(alloc_size(s))
#define SEQMALLOC_ALLOC_SIZE2(s1, s2) SEQMALLOC_ATTR(alloc_size(s1, s2))
#define SEQMALLOC_EXPORT SEQMALLOC_ATTR(visibility("default"))
#define SEQMALLOC_NOTHROW SEQMALLOC_ATTR(nothrow)

#define seq_malloc malloc
#define seq_free free
#define seq_calloc calloc
#define seq_realloc realloc
#define seq_posix_memalign posix_memalign
#define seq_aligned_alloc aligned_alloc
#define seq_valloc valloc
#define seq_memalign memalign
#define seq_pvalloc pvalloc

// called on process init/exit
void seq_malloc_initialize();
void seq_malloc_finalize();
// called on thread enter/exit
void seq_malloc_thread_initialize();
void seq_malloc_thread_finalize();

// exports
// malloc interface
void* seq_malloc(size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ALLOC_SIZE(1);
void seq_free(void* ptr)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW;
void* seq_calloc(size_t n, size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ALLOC_SIZE2(1, 2);
void* seq_realloc(void* ptr, size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ALLOC_SIZE(2);
// memory alignment ops
int seq_posix_memalign(void** memptr, size_t alignment, size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ATTR(nonnull(1))
    SEQMALLOC_ALLOC_SIZE(3);
void* seq_aligned_alloc(size_t alignment, size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ALLOC_SIZE(2);
void* seq_valloc(size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ALLOC_SIZE(1);
// obsolete alignment oos
void* seq_memalign(size_t alignment, size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ALLOC_SIZE(2);
void* seq_pvalloc(size_t size)
    SEQMALLOC_EXPORT SEQMALLOC_NOTHROW SEQMALLOC_ALLOC_SIZE(1);

#endif // __SEQMALLOC_H__
