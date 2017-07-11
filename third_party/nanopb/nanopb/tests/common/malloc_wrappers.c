#include "malloc_wrappers.h"
#include <stdint.h>
#include <assert.h>
#include <string.h>

static size_t alloc_count = 0;

/* Allocate memory and place check values before and after. */
void* malloc_with_check(size_t size)
{
    size_t size32 = (size + 3) / 4 + 3;
    uint32_t *buf = malloc(size32 * sizeof(uint32_t));
    buf[0] = size32;
    buf[1] = 0xDEADBEEF;
    buf[size32 - 1] = 0xBADBAD;
    return buf + 2;
}

/* Free memory allocated with malloc_with_check() and do the checks. */
void free_with_check(void *mem)
{
    uint32_t *buf = (uint32_t*)mem - 2;
    assert(buf[1] == 0xDEADBEEF);
    assert(buf[buf[0] - 1] == 0xBADBAD);
    free(buf);
}

/* Track memory usage */
void* counting_realloc(void *ptr, size_t size)
{
    /* Don't allocate crazy amounts of RAM when fuzzing */
    if (size > 1000000)
        return NULL;

    if (!ptr && size)
        alloc_count++;
    
    return realloc(ptr, size);
}

void counting_free(void *ptr)
{
    if (ptr)
    {
        assert(alloc_count > 0);
        alloc_count--;
        free(ptr);
    }
}

size_t get_alloc_count()
{
    return alloc_count;
}
