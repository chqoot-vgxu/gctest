#include <setjmp.h>
#include <string.h>
#include "gc.h"

static size_t gc_hash(uintptr_t ptr) {
    return ptr >> 3;
}

static GC_ptr_t* gc_find_ptr(GC_ptr_t* items, size_t capacity, void* ptr) {
    size_t hash = gc_hash((uintptr_t) ptr);
    size_t i = hash % capacity;
    GC_ptr_t* tombstone = NULL;

    for (;;) {
        GC_ptr_t* gc_ptr = &items[i];

        if (gc_ptr->hash == 0) {
            if (gc_ptr->value == NULL) { // found empty ptr
                return tombstone == NULL ? gc_ptr : tombstone;
            } else { // found tombstone
                if (tombstone == NULL) tombstone = gc_ptr;
            }
        } else if (gc_ptr->hash == hash) {
            return gc_ptr;
        }

        i = (i + 1) % capacity;
    }
}

static void gc_adjust(GC* gc) {
    if (!gc->paused && gc->capacity != 0) {
        gc_run(gc);
    }

    if (gc->count < gc->capacity / gc->grow_factor) {
        return;
    }

    size_t old_capacity = gc->capacity;
    size_t new_capacity = old_capacity != 0 ? old_capacity * gc->grow_factor : 8;

    GC_ptr_t* items = calloc(new_capacity, sizeof(GC_ptr_t));
    gc->count = 0;
    for (size_t i = 0; i < gc->capacity; i++) {
        GC_ptr_t* gc_ptr = &gc->items[i];
        if (gc_ptr->hash == 0) continue;

        GC_ptr_t* dest = gc_find_ptr(items, new_capacity, gc_ptr->value);
        dest->flags = gc_ptr->flags;
        dest->hash = gc_ptr->hash;
        dest->size = gc_ptr->size;
        dest->dtor = gc_ptr->dtor;
        dest->value = gc_ptr->value;

        gc->count++;
    }

    free(gc->items);
    gc->items = items;
    gc->capacity = new_capacity;
}

static void gc_mark_ptr(GC* gc, void* ptr) {
    GC_ptr_t* gc_ptr = gc_find_ptr(gc->items, gc->capacity, ptr);

    if ((uintptr_t)ptr < gc->minptr || (uintptr_t)ptr > gc->maxptr) return;

    if (gc_ptr->flags & GC_MARK) return;
    gc_ptr->flags |= GC_MARK;

    if (gc_ptr->flags & GC_LEAF) return;

    for (size_t i = 0; i < gc_ptr->size/sizeof(void*); i++) {
        gc_mark_ptr(gc, ((void**)gc_ptr->value)[i]);
    }
}

static void _gc_mark_stack(GC* gc) {
    void* stack_top;
    void* top = &stack_top;
    void* bottom = gc->stack_bottom;

    if (bottom == top) return;

    if (bottom < top) {
        for (; top >= bottom; top = ((char*)top) - sizeof(void*)) {
            gc_mark_ptr(gc, *((void**)top));
        }
    }

    if (bottom > top) {
        for (; top < bottom; top = ((char*)top) + sizeof(void*)) {
            gc_mark_ptr(gc, *((void**)top));
        }
    }
}

static void (*volatile gc_mark_stack)(GC*) = _gc_mark_stack;

static void gc_mark_roots(GC* gc) {
    for (size_t i = 0; i < gc->capacity; i++) {
        GC_ptr_t* gc_ptr = &gc->items[i];
        if (gc_ptr->hash == 0) continue;
        if (gc_ptr->flags & GC_ROOT) {

            gc_ptr->flags |= GC_MARK;
            if (gc_ptr->flags & GC_LEAF) continue;

            for (size_t j = 0; j < gc_ptr->size/sizeof(void*); j++) {
                gc_mark_ptr(gc, ((void**)(gc_ptr->value))[j]);
            }
        }
    }
}

static void gc_mark(GC* gc) {
    gc_mark_roots(gc);

    jmp_buf env;
    memset(&env, 0, sizeof(env));
    setjmp(env);
    gc_mark_stack(gc);
}

static void gc_sweep(GC* gc) {
    for (size_t i = 0; i < gc->capacity; i++) {
        GC_ptr_t* gc_ptr = &gc->items[i];
        if (gc_ptr->hash == 0) continue;

        if (gc_ptr->flags & GC_MARK) {
            gc_ptr->flags &= ~GC_MARK;
        } else {
            if (gc_ptr->dtor != NULL) {
                (gc_ptr->dtor)(gc_ptr->value);
            }

            free(gc_ptr->value);
            gc_ptr->hash = 0;
            gc->count--;
        }
    }
}

void gc_run(GC* gc) {
    gc_mark(gc);
    gc_sweep(gc);
}

static void* gc_ptr_add(GC* gc, void* ptr, GC_flags_t flags, destructor dtor, size_t size) {
    if (gc->count + 1 > gc->capacity * gc->load_factor) {
        gc_adjust(gc);
    }

    gc->minptr = (uintptr_t)ptr < gc->minptr? (uintptr_t)ptr: gc->minptr;
    gc->maxptr = (uintptr_t)ptr > gc->maxptr? (uintptr_t)ptr: gc->maxptr;

    GC_ptr_t* gc_ptr = gc_find_ptr(gc->items, gc->capacity, ptr);
    if (gc_ptr->hash == 0)
        gc->count++;

    gc_ptr->flags = flags;
    gc_ptr->hash = gc_hash((uintptr_t) ptr);
    gc_ptr->size = size;
    gc_ptr->dtor = dtor;
    gc_ptr->value = ptr;

    return ptr;
}

static GC_ptr_t* gc_ptr_rem(GC* gc, void* ptr) {
    GC_ptr_t* gc_ptr = gc_find_ptr(gc->items, gc->capacity, ptr);

    gc_ptr->hash = 0;
    gc->count--;
    return gc_ptr;
}

void* gc_malloc_opt(GC* gc, GC_flags_t flags, destructor dtor, size_t size) {
    void* ptr = malloc(size);
    if (ptr == NULL) {
        return NULL;
    }

    return gc_ptr_add(gc, ptr, flags, dtor, size);
}

void* gc_malloc(GC* gc, size_t size) {
    return gc_malloc_opt(gc, 0, NULL, size);
}

void* gc_calloc_opt(GC* gc, GC_flags_t flags, destructor dtor, size_t count, size_t size) {
    void* ptr = calloc(count, size);
    if (ptr == NULL) {
        return NULL;
    }

    return gc_ptr_add(gc, ptr, flags, dtor, count * size);
}

void* gc_calloc(GC* gc, size_t count, size_t item_size) {
    return gc_calloc_opt(gc, 0, NULL, count, item_size);
}

void* gc_realloc_opt(GC* gc, void* ptr, GC_flags_t flags, destructor dtor, size_t size) {
    void* nptr = realloc(ptr, size);
    if (nptr == NULL) {
        return NULL;
    }

    if (nptr == ptr) {
        return nptr;
    }

    gc_ptr_rem(gc, ptr);
    return gc_ptr_add(gc, nptr, flags, dtor, size);
}

void* gc_realloc(GC* gc, void* ptr, size_t size) {
   return gc_realloc_opt(gc, ptr, 0, NULL, size);
}

void gc_free(GC* gc, void* ptr) {
    if (ptr == NULL) return;

    GC_ptr_t* gc_ptr = gc_ptr_rem(gc, ptr);
    if (gc_ptr->dtor != NULL)
        (gc_ptr->dtor)(ptr);

    free(ptr);
}

void gc_pause(GC* gc) {
    gc->paused = 1;
}

void gc_resume(GC* gc) {
    gc->paused = 0;
}

void gc_init(GC* gc, void* stack_bottom) {
    gc->stack_bottom = stack_bottom;
    gc->load_factor = 0.75;
    gc->grow_factor = 2;
    gc->minptr = UINTPTR_MAX;
    gc->maxptr = 0;
    gc->capacity = 0;
    gc->count = 0;
    gc->items = NULL;
}

void gc_end(GC* gc) {
    for (size_t i = 0; i < gc->capacity; i++) {
        GC_ptr_t* gc_ptr = &gc->items[i];
        if (gc_ptr->hash == 0) continue;

        if (gc_ptr->dtor != NULL) {
            (gc_ptr->dtor)(gc_ptr->value);
        }
        free(gc_ptr->value);
    }

    free(gc->items);
    gc->items = NULL;
    gc->capacity = 0;
    gc->count = 0;
    gc->minptr = UINTPTR_MAX;
    gc->maxptr = 0;
}

GC_stats_t gc_stats(GC* gc) {
    size_t size = 0;
    size_t count = 0;
    for (size_t i = 0; i < gc->capacity; i++) {
        GC_ptr_t* gc_ptr = &gc->items[i];
        if (gc_ptr->hash == 0) continue;

        size += gc_ptr->size;
        count++;
    }

    GC_stats_t gc_stats = {
        .total_heap_size = size + gc->capacity,
        .live_objects = count,
        .live_objects_size = size,
    };

    return gc_stats;
}
