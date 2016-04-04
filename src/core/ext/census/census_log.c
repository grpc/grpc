/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Available log space is divided up in blocks of
   CENSUS_LOG_2_MAX_RECORD_SIZE bytes. A block can be in one of the
   following three data structures:
   - Free blocks (free_block_list)
   - Blocks with unread data (dirty_block_list)
   - Blocks currently attached to cores (core_local_blocks[])

   census_log_start_write() moves a block from core_local_blocks[] to the
   end of dirty_block_list when block:
   - is out-of-space OR
   - has an incomplete record (an incomplete record occurs when a thread calls
     census_log_start_write() and is context-switched before calling
     census_log_end_write()
   So, blocks in dirty_block_list are ordered, from oldest to newest, by time
   when block is detached from the core.

   census_log_read_next() first iterates over dirty_block_list and then
   core_local_blocks[]. It moves completely read blocks from dirty_block_list
   to free_block_list. Blocks in core_local_blocks[] are not freed, even when
   completely read.

   If log is configured to discard old records and free_block_list is empty,
   census_log_start_write() iterates over dirty_block_list to allocate a
   new block. It moves the oldest available block (no pending read/write) to
   core_local_blocks[].

   core_local_block_struct is used to implement a map from core id to the block
   associated with that core. This mapping is advisory. It is possible that the
   block returned by this mapping is no longer associated with that core. This
   mapping is updated, lazily, by census_log_start_write().

   Locking in block struct:

   Exclusive g_log.lock must be held before calling any functions operatong on
   block structs except census_log_start_write() and
   census_log_end_write().

   Writes to a block are serialized via writer_lock.
   census_log_start_write() acquires this lock and
   census_log_end_write() releases it. On failure to acquire the lock,
   writer allocates a new block for the current core and updates
   core_local_block accordingly.

   Simultaneous read and write access is allowed. Reader can safely read up to
   committed bytes (bytes_committed).

   reader_lock protects the block, currently being read, from getting recycled.
   start_read() acquires reader_lock and end_read() releases the lock.

   Read/write access to a block is disabled via try_disable_access(). It returns
   with both writer_lock and reader_lock held. These locks are subsequently
   released by enable_access() to enable access to the block.

   A note on naming: Most function/struct names are prepended by cl_
   (shorthand for census_log). Further, functions that manipulate structures
   include the name of the structure, which will be passed as the first
   argument. E.g. cl_block_initialize() will initialize a cl_block.
*/
#include "src/core/ext/census/census_log.h"
#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>
#include <string.h>

/* End of platform specific code */

typedef struct census_log_block_list_struct {
  struct census_log_block_list_struct *next;
  struct census_log_block_list_struct *prev;
  struct census_log_block *block;
} cl_block_list_struct;

typedef struct census_log_block {
  /* Pointer to underlying buffer */
  char *buffer;
  gpr_atm writer_lock;
  gpr_atm reader_lock;
  /* Keeps completely written bytes. Declared atomic because accessed
     simultaneously by reader and writer. */
  gpr_atm bytes_committed;
  /* Bytes already read */
  int32_t bytes_read;
  /* Links for list */
  cl_block_list_struct link;
/* We want this structure to be cacheline aligned. We assume the following
   sizes for the various parts on 32/64bit systems:
   type                 32b size    64b size
   char*                   4           8
   3x gpr_atm             12          24
   int32_t               4           8 (assumes padding)
   cl_block_list_struct   12          24
   TOTAL                  32          64

   Depending on the size of our cacheline and the architecture, we
   selectively add char buffering to this structure. The size is checked
   via assert in census_log_initialize(). */
#if defined(GPR_ARCH_64)
#define CL_BLOCK_PAD_SIZE (GPR_CACHELINE_SIZE - 64)
#else
#if defined(GPR_ARCH_32)
#define CL_BLOCK_PAD_SIZE (GPR_CACHELINE_SIZE - 32)
#else
#error "Unknown architecture"
#endif
#endif
#if CL_BLOCK_PAD_SIZE > 0
  char padding[CL_BLOCK_PAD_SIZE];
#endif
} cl_block;

/* A list of cl_blocks, doubly-linked through cl_block::link. */
typedef struct census_log_block_list {
  int32_t count;           /* Number of items in list. */
  cl_block_list_struct ht; /* head/tail of linked list. */
} cl_block_list;

/* Cacheline aligned block pointers to avoid false sharing. Block pointer must
   be initialized via set_block(), before calling other functions */
typedef struct census_log_core_local_block {
  gpr_atm block;
/* Ensure cachline alignment: we assume sizeof(gpr_atm) == 4 or 8 */
#if defined(GPR_ARCH_64)
#define CL_CORE_LOCAL_BLOCK_PAD_SIZE (GPR_CACHELINE_SIZE - 8)
#else
#if defined(GPR_ARCH_32)
#define CL_CORE_LOCAL_BLOCK_PAD_SIZE (GPR_CACHELINE_SIZE - 4)
#else
#error "Unknown architecture"
#endif
#endif
#if CL_CORE_LOCAL_BLOCK_PAD_SIZE > 0
  char padding[CL_CORE_LOCAL_BLOCK_PAD_SIZE];
#endif
} cl_core_local_block;

struct census_log {
  int discard_old_records;
  /* Number of cores (aka hardware-contexts) */
  unsigned num_cores;
  /* number of CENSUS_LOG_2_MAX_RECORD_SIZE blocks in log */
  int32_t num_blocks;
  cl_block *blocks;                       /* Block metadata. */
  cl_core_local_block *core_local_blocks; /* Keeps core to block mappings. */
  gpr_mu lock;
  int initialized; /* has log been initialized? */
  /* Keeps the state of the reader iterator. A value of 0 indicates that
     iterator has reached the end. census_log_init_reader() resets the
     value to num_core to restart iteration. */
  uint32_t read_iterator_state;
  /* Points to the block being read. If non-NULL, the block is locked for
     reading (block_being_read_->reader_lock is held). */
  cl_block *block_being_read;
  /* A non-zero value indicates that log is full. */
  gpr_atm is_full;
  char *buffer;
  cl_block_list free_block_list;
  cl_block_list dirty_block_list;
  gpr_atm out_of_space_count;
};

/* Single internal log */
static struct census_log g_log;

/* Functions that operate on an atomic memory location used as a lock */

/* Returns non-zero if lock is acquired */
static int cl_try_lock(gpr_atm *lock) { return gpr_atm_acq_cas(lock, 0, 1); }

static void cl_unlock(gpr_atm *lock) { gpr_atm_rel_store(lock, 0); }

/* Functions that operate on cl_core_local_block's */

static void cl_core_local_block_set_block(cl_core_local_block *clb,
                                          cl_block *block) {
  gpr_atm_rel_store(&clb->block, (gpr_atm)block);
}

static cl_block *cl_core_local_block_get_block(cl_core_local_block *clb) {
  return (cl_block *)gpr_atm_acq_load(&clb->block);
}

/* Functions that operate on cl_block_list_struct's */

static void cl_block_list_struct_initialize(cl_block_list_struct *bls,
                                            cl_block *block) {
  bls->next = bls->prev = bls;
  bls->block = block;
}

/* Functions that operate on cl_block_list's */

static void cl_block_list_initialize(cl_block_list *list) {
  list->count = 0;
  cl_block_list_struct_initialize(&list->ht, NULL);
}

/* Returns head of *this, or NULL if empty. */
static cl_block *cl_block_list_head(cl_block_list *list) {
  return list->ht.next->block;
}

/* Insert element *e after *pos. */
static void cl_block_list_insert(cl_block_list *list, cl_block_list_struct *pos,
                                 cl_block_list_struct *e) {
  list->count++;
  e->next = pos->next;
  e->prev = pos;
  e->next->prev = e;
  e->prev->next = e;
}

/* Insert block at the head of the list */
static void cl_block_list_insert_at_head(cl_block_list *list, cl_block *block) {
  cl_block_list_insert(list, &list->ht, &block->link);
}

/* Insert block at the tail of the list */
static void cl_block_list_insert_at_tail(cl_block_list *list, cl_block *block) {
  cl_block_list_insert(list, list->ht.prev, &block->link);
}

/* Removes block *b. Requires *b be in the list. */
static void cl_block_list_remove(cl_block_list *list, cl_block *b) {
  list->count--;
  b->link.next->prev = b->link.prev;
  b->link.prev->next = b->link.next;
}

/* Functions that operate on cl_block's */

static void cl_block_initialize(cl_block *block, char *buffer) {
  block->buffer = buffer;
  gpr_atm_rel_store(&block->writer_lock, 0);
  gpr_atm_rel_store(&block->reader_lock, 0);
  gpr_atm_rel_store(&block->bytes_committed, 0);
  block->bytes_read = 0;
  cl_block_list_struct_initialize(&block->link, block);
}

/* Guards against exposing partially written buffer to the reader. */
static void cl_block_set_bytes_committed(cl_block *block,
                                         int32_t bytes_committed) {
  gpr_atm_rel_store(&block->bytes_committed, bytes_committed);
}

static int32_t cl_block_get_bytes_committed(cl_block *block) {
  return gpr_atm_acq_load(&block->bytes_committed);
}

/* Tries to disable future read/write access to this block. Succeeds if:
   - no in-progress write AND
   - no in-progress read AND
   - 'discard_data' set to true OR no unread data
   On success, clears the block state and returns with writer_lock_ and
   reader_lock_ held. These locks are released by a subsequent
   cl_block_access_enable() call. */
static int cl_block_try_disable_access(cl_block *block, int discard_data) {
  if (!cl_try_lock(&block->writer_lock)) {
    return 0;
  }
  if (!cl_try_lock(&block->reader_lock)) {
    cl_unlock(&block->writer_lock);
    return 0;
  }
  if (!discard_data &&
      (block->bytes_read != cl_block_get_bytes_committed(block))) {
    cl_unlock(&block->reader_lock);
    cl_unlock(&block->writer_lock);
    return 0;
  }
  cl_block_set_bytes_committed(block, 0);
  block->bytes_read = 0;
  return 1;
}

static void cl_block_enable_access(cl_block *block) {
  cl_unlock(&block->reader_lock);
  cl_unlock(&block->writer_lock);
}

/* Returns with writer_lock held. */
static void *cl_block_start_write(cl_block *block, size_t size) {
  int32_t bytes_committed;
  if (!cl_try_lock(&block->writer_lock)) {
    return NULL;
  }
  bytes_committed = cl_block_get_bytes_committed(block);
  if (bytes_committed + size > CENSUS_LOG_MAX_RECORD_SIZE) {
    cl_unlock(&block->writer_lock);
    return NULL;
  }
  return block->buffer + bytes_committed;
}

/* Releases writer_lock and increments committed bytes by 'bytes_written'.
  'bytes_written' must be <= 'size' specified in the corresponding
  StartWrite() call. This function is thread-safe. */
static void cl_block_end_write(cl_block *block, size_t bytes_written) {
  cl_block_set_bytes_committed(
      block, cl_block_get_bytes_committed(block) + bytes_written);
  cl_unlock(&block->writer_lock);
}

/* Returns a pointer to the first unread byte in buffer. The number of bytes
   available are returned in 'bytes_available'. Acquires reader lock that is
   released by a subsequent cl_block_end_read() call. Returns NULL if:
   - read in progress
   - no data available */
static void *cl_block_start_read(cl_block *block, size_t *bytes_available) {
  void *record;
  if (!cl_try_lock(&block->reader_lock)) {
    return NULL;
  }
  /* bytes_committed may change from under us. Use bytes_available to update
     bytes_read below. */
  *bytes_available = cl_block_get_bytes_committed(block) - block->bytes_read;
  if (*bytes_available == 0) {
    cl_unlock(&block->reader_lock);
    return NULL;
  }
  record = block->buffer + block->bytes_read;
  block->bytes_read += *bytes_available;
  return record;
}

static void cl_block_end_read(cl_block *block) {
  cl_unlock(&block->reader_lock);
}

/* Internal functions operating on g_log */

/* Allocates a new free block (or recycles an available dirty block if log is
   configured to discard old records). Returns NULL if out-of-space. */
static cl_block *cl_allocate_block(void) {
  cl_block *block = cl_block_list_head(&g_log.free_block_list);
  if (block != NULL) {
    cl_block_list_remove(&g_log.free_block_list, block);
    return block;
  }
  if (!g_log.discard_old_records) {
    /* No free block and log is configured to keep old records. */
    return NULL;
  }
  /* Recycle dirty block. Start from the oldest. */
  for (block = cl_block_list_head(&g_log.dirty_block_list); block != NULL;
       block = block->link.next->block) {
    if (cl_block_try_disable_access(block, 1 /* discard data */)) {
      cl_block_list_remove(&g_log.dirty_block_list, block);
      return block;
    }
  }
  return NULL;
}

/* Allocates a new block and updates core id => block mapping. 'old_block'
   points to the block that the caller thinks is attached to
   'core_id'. 'old_block' may be NULL. Returns non-zero if:
   - allocated a new block OR
   - 'core_id' => 'old_block' mapping changed (another thread allocated a
     block before lock was acquired). */
static int cl_allocate_core_local_block(int32_t core_id, cl_block *old_block) {
  /* Now that we have the lock, check if core-local mapping has changed. */
  cl_core_local_block *core_local_block = &g_log.core_local_blocks[core_id];
  cl_block *block = cl_core_local_block_get_block(core_local_block);
  if ((block != NULL) && (block != old_block)) {
    return 1;
  }
  if (block != NULL) {
    cl_core_local_block_set_block(core_local_block, NULL);
    cl_block_list_insert_at_tail(&g_log.dirty_block_list, block);
  }
  block = cl_allocate_block();
  if (block == NULL) {
    gpr_atm_rel_store(&g_log.is_full, 1);
    return 0;
  }
  cl_core_local_block_set_block(core_local_block, block);
  cl_block_enable_access(block);
  return 1;
}

static cl_block *cl_get_block(void *record) {
  uintptr_t p = (uintptr_t)((char *)record - g_log.buffer);
  uintptr_t index = p >> CENSUS_LOG_2_MAX_RECORD_SIZE;
  return &g_log.blocks[index];
}

/* Gets the next block to read and tries to free 'prev' block (if not NULL).
   Returns NULL if reached the end. */
static cl_block *cl_next_block_to_read(cl_block *prev) {
  cl_block *block = NULL;
  if (g_log.read_iterator_state == g_log.num_cores) {
    /* We are traversing dirty list; find the next dirty block. */
    if (prev != NULL) {
      /* Try to free the previous block if there is no unread data. This block
         may have unread data if previously incomplete record completed between
         read_next() calls. */
      block = prev->link.next->block;
      if (cl_block_try_disable_access(prev, 0 /* do not discard data */)) {
        cl_block_list_remove(&g_log.dirty_block_list, prev);
        cl_block_list_insert_at_head(&g_log.free_block_list, prev);
        gpr_atm_rel_store(&g_log.is_full, 0);
      }
    } else {
      block = cl_block_list_head(&g_log.dirty_block_list);
    }
    if (block != NULL) {
      return block;
    }
    /* We are done with the dirty list; moving on to core-local blocks. */
  }
  while (g_log.read_iterator_state > 0) {
    g_log.read_iterator_state--;
    block = cl_core_local_block_get_block(
        &g_log.core_local_blocks[g_log.read_iterator_state]);
    if (block != NULL) {
      return block;
    }
  }
  return NULL;
}

/* External functions: primary stats_log interface */
void census_log_initialize(size_t size_in_mb, int discard_old_records) {
  int32_t ix;
  /* Check cacheline alignment. */
  GPR_ASSERT(sizeof(cl_block) % GPR_CACHELINE_SIZE == 0);
  GPR_ASSERT(sizeof(cl_core_local_block) % GPR_CACHELINE_SIZE == 0);
  GPR_ASSERT(!g_log.initialized);
  g_log.discard_old_records = discard_old_records;
  g_log.num_cores = gpr_cpu_num_cores();
  /* Ensure at least as many blocks as there are cores. */
  g_log.num_blocks = GPR_MAX(
      g_log.num_cores, (size_in_mb << 20) >> CENSUS_LOG_2_MAX_RECORD_SIZE);
  gpr_mu_init(&g_log.lock);
  g_log.read_iterator_state = 0;
  g_log.block_being_read = NULL;
  gpr_atm_rel_store(&g_log.is_full, 0);
  g_log.core_local_blocks = (cl_core_local_block *)gpr_malloc_aligned(
      g_log.num_cores * sizeof(cl_core_local_block), GPR_CACHELINE_SIZE_LOG);
  memset(g_log.core_local_blocks, 0,
         g_log.num_cores * sizeof(cl_core_local_block));
  g_log.blocks = (cl_block *)gpr_malloc_aligned(
      g_log.num_blocks * sizeof(cl_block), GPR_CACHELINE_SIZE_LOG);
  memset(g_log.blocks, 0, g_log.num_blocks * sizeof(cl_block));
  g_log.buffer = gpr_malloc(g_log.num_blocks * CENSUS_LOG_MAX_RECORD_SIZE);
  memset(g_log.buffer, 0, g_log.num_blocks * CENSUS_LOG_MAX_RECORD_SIZE);
  cl_block_list_initialize(&g_log.free_block_list);
  cl_block_list_initialize(&g_log.dirty_block_list);
  for (ix = 0; ix < g_log.num_blocks; ++ix) {
    cl_block *block = g_log.blocks + ix;
    cl_block_initialize(block,
                        g_log.buffer + (CENSUS_LOG_MAX_RECORD_SIZE * ix));
    cl_block_try_disable_access(block, 1 /* discard data */);
    cl_block_list_insert_at_tail(&g_log.free_block_list, block);
  }
  gpr_atm_rel_store(&g_log.out_of_space_count, 0);
  g_log.initialized = 1;
}

void census_log_shutdown(void) {
  GPR_ASSERT(g_log.initialized);
  gpr_mu_destroy(&g_log.lock);
  gpr_free_aligned(g_log.core_local_blocks);
  g_log.core_local_blocks = NULL;
  gpr_free_aligned(g_log.blocks);
  g_log.blocks = NULL;
  gpr_free(g_log.buffer);
  g_log.buffer = NULL;
  g_log.initialized = 0;
}

void *census_log_start_write(size_t size) {
  /* Used to bound number of times block allocation is attempted. */
  int32_t attempts_remaining = g_log.num_blocks;
  /* TODO(aveitch): move this inside the do loop when current_cpu is fixed */
  int32_t core_id = gpr_cpu_current_cpu();
  GPR_ASSERT(g_log.initialized);
  if (size > CENSUS_LOG_MAX_RECORD_SIZE) {
    return NULL;
  }
  do {
    int allocated;
    void *record = NULL;
    cl_block *block =
        cl_core_local_block_get_block(&g_log.core_local_blocks[core_id]);
    if (block && (record = cl_block_start_write(block, size))) {
      return record;
    }
    /* Need to allocate a new block. We are here if:
       - No block associated with the core OR
       - Write in-progress on the block OR
       - block is out of space */
    if (gpr_atm_acq_load(&g_log.is_full)) {
      gpr_atm_no_barrier_fetch_add(&g_log.out_of_space_count, 1);
      return NULL;
    }
    gpr_mu_lock(&g_log.lock);
    allocated = cl_allocate_core_local_block(core_id, block);
    gpr_mu_unlock(&g_log.lock);
    if (!allocated) {
      gpr_atm_no_barrier_fetch_add(&g_log.out_of_space_count, 1);
      return NULL;
    }
  } while (attempts_remaining--);
  /* Give up. */
  gpr_atm_no_barrier_fetch_add(&g_log.out_of_space_count, 1);
  return NULL;
}

void census_log_end_write(void *record, size_t bytes_written) {
  GPR_ASSERT(g_log.initialized);
  cl_block_end_write(cl_get_block(record), bytes_written);
}

void census_log_init_reader(void) {
  GPR_ASSERT(g_log.initialized);
  gpr_mu_lock(&g_log.lock);
  /* If a block is locked for reading unlock it. */
  if (g_log.block_being_read != NULL) {
    cl_block_end_read(g_log.block_being_read);
    g_log.block_being_read = NULL;
  }
  g_log.read_iterator_state = g_log.num_cores;
  gpr_mu_unlock(&g_log.lock);
}

const void *census_log_read_next(size_t *bytes_available) {
  GPR_ASSERT(g_log.initialized);
  gpr_mu_lock(&g_log.lock);
  if (g_log.block_being_read != NULL) {
    cl_block_end_read(g_log.block_being_read);
  }
  do {
    g_log.block_being_read = cl_next_block_to_read(g_log.block_being_read);
    if (g_log.block_being_read != NULL) {
      void *record =
          cl_block_start_read(g_log.block_being_read, bytes_available);
      if (record != NULL) {
        gpr_mu_unlock(&g_log.lock);
        return record;
      }
    }
  } while (g_log.block_being_read != NULL);
  gpr_mu_unlock(&g_log.lock);
  return NULL;
}

size_t census_log_remaining_space(void) {
  size_t space;
  GPR_ASSERT(g_log.initialized);
  gpr_mu_lock(&g_log.lock);
  if (g_log.discard_old_records) {
    /* Remaining space is not meaningful; just return the entire log space. */
    space = g_log.num_blocks << CENSUS_LOG_2_MAX_RECORD_SIZE;
  } else {
    space = g_log.free_block_list.count * CENSUS_LOG_MAX_RECORD_SIZE;
  }
  gpr_mu_unlock(&g_log.lock);
  return space;
}

int census_log_out_of_space_count(void) {
  GPR_ASSERT(g_log.initialized);
  return gpr_atm_acq_load(&g_log.out_of_space_count);
}
