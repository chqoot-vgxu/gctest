#include <stdio.h>
#include <string.h>
#include "gc.h"

static GC gc;

char* make_string() {
    char* string = gc_malloc(&gc, 14);
    strcpy(string, "Hello, world!");
    return string;
}

int main(int argc, char* argv[]) {
    gc_init(&gc, &argc);

    char* string = make_string();
    printf("%s No leaks!\n", string);

    GC_stats_t stats = gc_stats(&gc);
    printf("stats = {\n");
    printf("    total_heap_size (B): %lu,\n", stats.total_heap_size);
    printf("    live_objects: %lu,\n", stats.live_objects);
    printf("    live_objects_size (B): %lu,\n", stats.live_objects_size);
    printf("}\n");

    gc_end(&gc);
    return 0;
}
