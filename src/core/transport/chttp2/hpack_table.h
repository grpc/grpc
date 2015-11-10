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

#ifndef GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_HPACK_TABLE_H
#define GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_HPACK_TABLE_H

#include "src/core/transport/metadata.h"
#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>

/* HPACK header table */

/* last index in the static table */
#define GRPC_CHTTP2_LAST_STATIC_ENTRY 61

/* Initial table size as per the spec */
#define GRPC_CHTTP2_INITIAL_HPACK_TABLE_SIZE 4096
/* Maximum table size that we'll use */
#define GRPC_CHTTP2_MAX_HPACK_TABLE_SIZE GRPC_CHTTP2_INITIAL_HPACK_TABLE_SIZE
/* Per entry overhead bytes as per the spec */
#define GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD 32
#if 0
/* Maximum number of entries we could possibly fit in the table, given defined
   overheads */
#define GRPC_CHTTP2_MAX_TABLE_COUNT                                            \
  ((GRPC_CHTTP2_MAX_HPACK_TABLE_SIZE + GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD - 1) / \
   GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD)
#endif

/* hpack decoder table */
typedef struct {
  grpc_mdctx *mdctx;
  /* the first used entry in ents */
  gpr_uint32 first_ent;
  /* the last used entry in ents */
  gpr_uint32 last_ent;
  /* how many entries are in the table */
  gpr_uint32 num_ents;
  /* the amount of memory used by the table, according to the hpack algorithm */
  gpr_uint32 mem_used;
  /* the max memory allowed to be used by the table, according to the hpack
     algorithm */
  gpr_uint32 max_bytes;
  /* the currently agreed size of the table, according to the hpack algorithm */
  gpr_uint32 current_table_bytes;
  /* Maximum number of entries we could possibly fit in the table, given defined
     overheads */
  gpr_uint32 max_entries;
  /* Number of entries allocated in ents */
  gpr_uint32 cap_entries;
  /* a circular buffer of headers - this is stored in the opposite order to
     what hpack specifies, in order to simplify table management a little...
     meaning lookups need to SUBTRACT from the end position */
  grpc_mdelem **ents;
  grpc_mdelem *static_ents[GRPC_CHTTP2_LAST_STATIC_ENTRY];
} grpc_chttp2_hptbl;

/* initialize a hpack table */
void grpc_chttp2_hptbl_init(grpc_chttp2_hptbl *tbl, grpc_mdctx *mdctx);
void grpc_chttp2_hptbl_destroy(grpc_chttp2_hptbl *tbl);
void grpc_chttp2_hptbl_set_max_bytes(grpc_chttp2_hptbl *tbl,
                                     gpr_uint32 max_bytes);
int grpc_chttp2_hptbl_set_current_table_size(grpc_chttp2_hptbl *tbl,
                                             gpr_uint32 bytes);

/* lookup a table entry based on its hpack index */
grpc_mdelem *grpc_chttp2_hptbl_lookup(const grpc_chttp2_hptbl *tbl,
                                      gpr_uint32 index);
/* add a table entry to the index */
int grpc_chttp2_hptbl_add(grpc_chttp2_hptbl *tbl,
                          grpc_mdelem *md) GRPC_MUST_USE_RESULT;
/* Find a key/value pair in the table... returns the index in the table of the
   most similar entry, or 0 if the value was not found */
typedef struct {
  gpr_uint32 index;
  int has_value;
} grpc_chttp2_hptbl_find_result;
grpc_chttp2_hptbl_find_result grpc_chttp2_hptbl_find(
    const grpc_chttp2_hptbl *tbl, grpc_mdelem *md);

#endif /* GRPC_INTERNAL_CORE_TRANSPORT_CHTTP2_HPACK_TABLE_H */
