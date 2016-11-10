/*
--------------------------------------------------------------------
By Bob Jenkins, September 1996.  recycle.h
You may use this code in any way you wish, and it is free.  No warranty.

This manages memory for commonly-allocated structures.
It allocates RESTART to REMAX items at a time.
Timings have shown that, if malloc is used for every new structure,
  malloc will consume about 90% of the time in a program.  This
  module cuts down the number of mallocs by an order of magnitude.
This also decreases memory fragmentation, and freeing all structures
  only requires freeing the root.
--------------------------------------------------------------------
*/

#ifndef STANDARD
#include "standard.h"
#endif

#ifndef RECYCLE
#define RECYCLE

#define RESTART    0
#define REMAX      32000

struct recycle
{
   struct recycle *next;
};
typedef  struct recycle  recycle;

struct reroot
{
   struct recycle *list;     /* list of malloced blocks */
   struct recycle *trash;    /* list of deleted items */
   size_t          size;     /* size of an item */
   size_t          logsize;  /* log_2 of number of items in a block */
   word            numleft;  /* number of bytes left in this block */
};
typedef  struct reroot  reroot;

/* make a new recycling root */
reroot  *remkroot(/*_ size_t mysize _*/);

/* free a recycling root and all the items it has made */
void     refree(/*_ struct reroot *r _*/);

/* get a new (cleared) item from the root */
#define renew(r) ((r)->numleft ? \
   (((char *)((r)->list+1))+((r)->numleft-=(r)->size)) : renewx(r))

char    *renewx(/*_ struct reroot *r _*/);

/* delete an item; let the root recycle it */
/* void     redel(/o_ struct reroot *r, struct recycle *item _o/); */
#define redel(root,item) { \
   ((recycle *)item)->next=(root)->trash; \
   (root)->trash=(recycle *)(item); \
}

/* malloc, but complain to stderr and exit program if no joy */
/* use plain free() to free memory allocated by remalloc() */
char    *remalloc(/*_ size_t len, char *purpose _*/);

#endif  /* RECYCLE */
