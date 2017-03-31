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

#include "src/core/ext/transport/chttp2/transport/hpack_table.h"

#include <assert.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/support/murmur_hash.h"

extern int grpc_http_trace;

static struct {
  const char *key;
  const char *value;
} static_table[] = {
    /* 0: */
    {NULL, NULL},
    /* 1: */
    {":authority", ""},
    /* 2: */
    {":method", "GET"},
    /* 3: */
    {":method", "POST"},
    /* 4: */
    {":path", "/"},
    /* 5: */
    {":path", "/index.html"},
    /* 6: */
    {":scheme", "http"},
    /* 7: */
    {":scheme", "https"},
    /* 8: */
    {":status", "200"},
    /* 9: */
    {":status", "204"},
    /* 10: */
    {":status", "206"},
    /* 11: */
    {":status", "304"},
    /* 12: */
    {":status", "400"},
    /* 13: */
    {":status", "404"},
    /* 14: */
    {":status", "500"},
    /* 15: */
    {"accept-charset", ""},
    /* 16: */
    {"accept-encoding", "gzip, deflate"},
    /* 17: */
    {"accept-language", ""},
    /* 18: */
    {"accept-ranges", ""},
    /* 19: */
    {"accept", ""},
    /* 20: */
    {"access-control-allow-origin", ""},
    /* 21: */
    {"age", ""},
    /* 22: */
    {"allow", ""},
    /* 23: */
    {"authorization", ""},
    /* 24: */
    {"cache-control", ""},
    /* 25: */
    {"content-disposition", ""},
    /* 26: */
    {"content-encoding", ""},
    /* 27: */
    {"content-language", ""},
    /* 28: */
    {"content-length", ""},
    /* 29: */
    {"content-location", ""},
    /* 30: */
    {"content-range", ""},
    /* 31: */
    {"content-type", ""},
    /* 32: */
    {"cookie", ""},
    /* 33: */
    {"date", ""},
    /* 34: */
    {"etag", ""},
    /* 35: */
    {"expect", ""},
    /* 36: */
    {"expires", ""},
    /* 37: */
    {"from", ""},
    /* 38: */
    {"host", ""},
    /* 39: */
    {"if-match", ""},
    /* 40: */
    {"if-modified-since", ""},
    /* 41: */
    {"if-none-match", ""},
    /* 42: */
    {"if-range", ""},
    /* 43: */
    {"if-unmodified-since", ""},
    /* 44: */
    {"last-modified", ""},
    /* 45: */
    {"link", ""},
    /* 46: */
    {"location", ""},
    /* 47: */
    {"max-forwards", ""},
    /* 48: */
    {"proxy-authenticate", ""},
    /* 49: */
    {"proxy-authorization", ""},
    /* 50: */
    {"range", ""},
    /* 51: */
    {"referer", ""},
    /* 52: */
    {"refresh", ""},
    /* 53: */
    {"retry-after", ""},
    /* 54: */
    {"server", ""},
    /* 55: */
    {"set-cookie", ""},
    /* 56: */
    {"strict-transport-security", ""},
    /* 57: */
    {"transfer-encoding", ""},
    /* 58: */
    {"user-agent", ""},
    /* 59: */
    {"vary", ""},
    /* 60: */
    {"via", ""},
    /* 61: */
    {"www-authenticate", ""},
};

static uint32_t entries_for_bytes(uint32_t bytes) {
  return (bytes + GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD - 1) /
         GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD;
}

void grpc_chttp2_hptbl_init(grpc_exec_ctx *exec_ctx, grpc_chttp2_hptbl *tbl) {
  size_t i;

  memset(tbl, 0, sizeof(*tbl));
  tbl->current_table_bytes = tbl->max_bytes =
      GRPC_CHTTP2_INITIAL_HPACK_TABLE_SIZE;
  tbl->max_entries = tbl->cap_entries =
      entries_for_bytes(tbl->current_table_bytes);
  tbl->ents = gpr_malloc(sizeof(*tbl->ents) * tbl->cap_entries);
  memset(tbl->ents, 0, sizeof(*tbl->ents) * tbl->cap_entries);
  for (i = 1; i <= GRPC_CHTTP2_LAST_STATIC_ENTRY; i++) {
    tbl->static_ents[i - 1] = grpc_mdelem_from_slices(
        exec_ctx,
        grpc_slice_intern(grpc_slice_from_static_string(static_table[i].key)),
        grpc_slice_intern(
            grpc_slice_from_static_string(static_table[i].value)));
  }
}

void grpc_chttp2_hptbl_destroy(grpc_exec_ctx *exec_ctx,
                               grpc_chttp2_hptbl *tbl) {
  size_t i;
  for (i = 0; i < GRPC_CHTTP2_LAST_STATIC_ENTRY; i++) {
    GRPC_MDELEM_UNREF(exec_ctx, tbl->static_ents[i]);
  }
  for (i = 0; i < tbl->num_ents; i++) {
    GRPC_MDELEM_UNREF(exec_ctx,
                      tbl->ents[(tbl->first_ent + i) % tbl->cap_entries]);
  }
  gpr_free(tbl->ents);
}

grpc_mdelem grpc_chttp2_hptbl_lookup(const grpc_chttp2_hptbl *tbl,
                                     uint32_t tbl_index) {
  /* Static table comes first, just return an entry from it */
  if (tbl_index <= GRPC_CHTTP2_LAST_STATIC_ENTRY) {
    return tbl->static_ents[tbl_index - 1];
  }
  /* Otherwise, find the value in the list of valid entries */
  tbl_index -= (GRPC_CHTTP2_LAST_STATIC_ENTRY + 1);
  if (tbl_index < tbl->num_ents) {
    uint32_t offset =
        (tbl->num_ents - 1u - tbl_index + tbl->first_ent) % tbl->cap_entries;
    return tbl->ents[offset];
  }
  /* Invalid entry: return error */
  return GRPC_MDNULL;
}

/* Evict one element from the table */
static void evict1(grpc_exec_ctx *exec_ctx, grpc_chttp2_hptbl *tbl) {
  grpc_mdelem first_ent = tbl->ents[tbl->first_ent];
  size_t elem_bytes = GRPC_SLICE_LENGTH(GRPC_MDKEY(first_ent)) +
                      GRPC_SLICE_LENGTH(GRPC_MDVALUE(first_ent)) +
                      GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD;
  GPR_ASSERT(elem_bytes <= tbl->mem_used);
  tbl->mem_used -= (uint32_t)elem_bytes;
  tbl->first_ent = ((tbl->first_ent + 1) % tbl->cap_entries);
  tbl->num_ents--;
  GRPC_MDELEM_UNREF(exec_ctx, first_ent);
}

static void rebuild_ents(grpc_chttp2_hptbl *tbl, uint32_t new_cap) {
  grpc_mdelem *ents = gpr_malloc(sizeof(*ents) * new_cap);
  uint32_t i;

  for (i = 0; i < tbl->num_ents; i++) {
    ents[i] = tbl->ents[(tbl->first_ent + i) % tbl->cap_entries];
  }
  gpr_free(tbl->ents);
  tbl->ents = ents;
  tbl->cap_entries = new_cap;
  tbl->first_ent = 0;
}

void grpc_chttp2_hptbl_set_max_bytes(grpc_exec_ctx *exec_ctx,
                                     grpc_chttp2_hptbl *tbl,
                                     uint32_t max_bytes) {
  if (tbl->max_bytes == max_bytes) {
    return;
  }
  if (grpc_http_trace) {
    gpr_log(GPR_DEBUG, "Update hpack parser max size to %d", max_bytes);
  }
  while (tbl->mem_used > max_bytes) {
    evict1(exec_ctx, tbl);
  }
  tbl->max_bytes = max_bytes;
}

grpc_error *grpc_chttp2_hptbl_set_current_table_size(grpc_exec_ctx *exec_ctx,
                                                     grpc_chttp2_hptbl *tbl,
                                                     uint32_t bytes) {
  if (tbl->current_table_bytes == bytes) {
    return GRPC_ERROR_NONE;
  }
  if (bytes > tbl->max_bytes) {
    char *msg;
    gpr_asprintf(&msg,
                 "Attempt to make hpack table %d bytes when max is %d bytes",
                 bytes, tbl->max_bytes);
    grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }
  if (grpc_http_trace) {
    gpr_log(GPR_DEBUG, "Update hpack parser table size to %d", bytes);
  }
  while (tbl->mem_used > bytes) {
    evict1(exec_ctx, tbl);
  }
  tbl->current_table_bytes = bytes;
  tbl->max_entries = entries_for_bytes(bytes);
  if (tbl->max_entries > tbl->cap_entries) {
    rebuild_ents(tbl, GPR_MAX(tbl->max_entries, 2 * tbl->cap_entries));
  } else if (tbl->max_entries < tbl->cap_entries / 3) {
    uint32_t new_cap = GPR_MAX(tbl->max_entries, 16u);
    if (new_cap != tbl->cap_entries) {
      rebuild_ents(tbl, new_cap);
    }
  }
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_chttp2_hptbl_add(grpc_exec_ctx *exec_ctx,
                                  grpc_chttp2_hptbl *tbl, grpc_mdelem md) {
  /* determine how many bytes of buffer this entry represents */
  size_t elem_bytes = GRPC_SLICE_LENGTH(GRPC_MDKEY(md)) +
                      GRPC_SLICE_LENGTH(GRPC_MDVALUE(md)) +
                      GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD;

  if (tbl->current_table_bytes > tbl->max_bytes) {
    char *msg;
    gpr_asprintf(
        &msg,
        "HPACK max table size reduced to %d but not reflected by hpack "
        "stream (still at %d)",
        tbl->max_bytes, tbl->current_table_bytes);
    grpc_error *err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    return err;
  }

  /* we can't add elements bigger than the max table size */
  if (elem_bytes > tbl->current_table_bytes) {
    /* HPACK draft 10 section 4.4 states:
     * If the size of the new entry is less than or equal to the maximum
     * size, that entry is added to the table.  It is not an error to
     * attempt to add an entry that is larger than the maximum size; an
     * attempt to add an entry larger than the entire table causes
     * the table
     * to be emptied of all existing entries, and results in an
     * empty table.
     */
    while (tbl->num_ents) {
      evict1(exec_ctx, tbl);
    }
    return GRPC_ERROR_NONE;
  }

  /* evict entries to ensure no overflow */
  while (elem_bytes > (size_t)tbl->current_table_bytes - tbl->mem_used) {
    evict1(exec_ctx, tbl);
  }

  /* copy the finalized entry in */
  tbl->ents[(tbl->first_ent + tbl->num_ents) % tbl->cap_entries] =
      GRPC_MDELEM_REF(md);

  /* update accounting values */
  tbl->num_ents++;
  tbl->mem_used += (uint32_t)elem_bytes;
  return GRPC_ERROR_NONE;
}

grpc_chttp2_hptbl_find_result grpc_chttp2_hptbl_find(
    const grpc_chttp2_hptbl *tbl, grpc_mdelem md) {
  grpc_chttp2_hptbl_find_result r = {0, 0};
  uint32_t i;

  /* See if the string is in the static table */
  for (i = 0; i < GRPC_CHTTP2_LAST_STATIC_ENTRY; i++) {
    grpc_mdelem ent = tbl->static_ents[i];
    if (!grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDKEY(ent))) continue;
    r.index = i + 1u;
    r.has_value = grpc_slice_eq(GRPC_MDVALUE(md), GRPC_MDVALUE(ent));
    if (r.has_value) return r;
  }

  /* Scan the dynamic table */
  for (i = 0; i < tbl->num_ents; i++) {
    uint32_t idx =
        (uint32_t)(tbl->num_ents - i + GRPC_CHTTP2_LAST_STATIC_ENTRY);
    grpc_mdelem ent = tbl->ents[(tbl->first_ent + i) % tbl->cap_entries];
    if (!grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDKEY(ent))) continue;
    r.index = idx;
    r.has_value = grpc_slice_eq(GRPC_MDVALUE(md), GRPC_MDVALUE(ent));
    if (r.has_value) return r;
  }

  return r;
}
