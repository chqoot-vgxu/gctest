#include <stdlib.h>
#include <stdint.h>

typedef void (*destructor)(void*);

typedef enum GC_flags_t {
    GC_MARK  = 0x01,
    GC_ROOT  = 0x02,
    GC_LEAF  = 0x04,
} GC_flags_t;

typedef struct GC_ptr_t {
    GC_flags_t flags;
    size_t hash;
    size_t size;
    destructor dtor;
    void* value;
} GC_ptr_t;

typedef struct GC {
    void* stack_bottom;
    double load_factor;
    double grow_factor;
    int paused;
    uintptr_t minptr, maxptr;
    size_t capacity, count;
    GC_ptr_t* items;
} GC;

typedef struct GC_stats {
    size_t total_heap_size;
    size_t live_objects;
    size_t live_objects_size;
} GC_stats_t;

void gc_run(GC* gc);
void* gc_malloc_opt(GC* gc, GC_flags_t flags, destructor dtor, size_t size); 
void* gc_malloc(GC* gc, size_t size);
void* gc_calloc_opt(GC* gc, GC_flags_t flags, destructor dtor, size_t count, size_t size); 
void* gc_calloc(GC* gc, size_t count, size_t size);
void* gc_realloc_opt(GC* gc, void* ptr, GC_flags_t flags, destructor dtor, size_t size); 
void* gc_realloc(GC* gc, void* ptr, size_t size);
void gc_free(GC* gc, void* ptr);
void gc_pause(GC* gc);
void gc_resume(GC* gc);
void gc_init(GC* gc, void* stack_bottom);
void gc_end(GC* gc);
GC_stats_t gc_stats(GC* gc);
