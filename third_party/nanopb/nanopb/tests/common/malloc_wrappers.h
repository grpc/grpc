#include <stdlib.h>

void* malloc_with_check(size_t size);
void free_with_check(void *mem);
void* counting_realloc(void *ptr, size_t size);
void counting_free(void *ptr);
size_t get_alloc_count();
