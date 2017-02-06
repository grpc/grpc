/* This is just a wrapper in order to get our own malloc wrappers into nanopb core. */

#define pb_realloc(ptr,size) counting_realloc(ptr,size)
#define pb_free(ptr) counting_free(ptr)

#ifdef PB_OLD_SYSHDR
#include PB_OLD_SYSHDR
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#endif

#include <malloc_wrappers.h>
