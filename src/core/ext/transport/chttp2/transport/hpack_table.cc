/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_table.h"

#include <assert.h>
#include <string.h>

#include "absl/strings/str_format.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/murmur_hash.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/lib/transport/static_metadata.h"

extern grpc_core::TraceFlag grpc_http_trace;

void grpc_chttp2_hptbl_destroy(grpc_chttp2_hptbl* tbl) {
  size_t i;
  for (i = 0; i < tbl->num_ents; i++) {
    GRPC_MDELEM_UNREF(tbl->ents[(tbl->first_ent + i) % tbl->cap_entries]);
  }
  gpr_free(tbl->ents);
  tbl->ents = nullptr;
}

template <bool take_ref>
static grpc_mdelem lookup_dynamic_index(const grpc_chttp2_hptbl* tbl,
                                        uint32_t tbl_index) {
  /* Not static - find the value in the list of valid entries */
  tbl_index -= (GRPC_CHTTP2_LAST_STATIC_ENTRY + 1);
  if (tbl_index < tbl->num_ents) {
    uint32_t offset =
        (tbl->num_ents - 1u - tbl_index + tbl->first_ent) % tbl->cap_entries;
    grpc_mdelem md = tbl->ents[offset];
    if (take_ref) {
      GRPC_MDELEM_REF(md);
    }
    return md;
  }
  /* Invalid entry: return error */
  return GRPC_MDNULL;
}

grpc_mdelem grpc_chttp2_hptbl_lookup_dynamic_index(const grpc_chttp2_hptbl* tbl,
                                                   uint32_t tbl_index) {
  return lookup_dynamic_index<false>(tbl, tbl_index);
}

grpc_mdelem grpc_chttp2_hptbl_lookup_ref_dynamic_index(
    const grpc_chttp2_hptbl* tbl, uint32_t tbl_index) {
  return lookup_dynamic_index<true>(tbl, tbl_index);
}

/* Evict one element from the table */
static void evict1(grpc_chttp2_hptbl* tbl) {
  grpc_mdelem first_ent = tbl->ents[tbl->first_ent];
  size_t elem_bytes = GRPC_SLICE_LENGTH(GRPC_MDKEY(first_ent)) +
                      GRPC_SLICE_LENGTH(GRPC_MDVALUE(first_ent)) +
                      GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD;
  GPR_ASSERT(elem_bytes <= tbl->mem_used);
  tbl->mem_used -= static_cast<uint32_t>(elem_bytes);
  tbl->first_ent = ((tbl->first_ent + 1) % tbl->cap_entries);
  tbl->num_ents--;
  GRPC_MDELEM_UNREF(first_ent);
}

static void rebuild_ents(grpc_chttp2_hptbl* tbl, uint32_t new_cap) {
  grpc_mdelem* ents =
      static_cast<grpc_mdelem*>(gpr_malloc(sizeof(*ents) * new_cap));
  uint32_t i;

  for (i = 0; i < tbl->num_ents; i++) {
    ents[i] = tbl->ents[(tbl->first_ent + i) % tbl->cap_entries];
  }
  gpr_free(tbl->ents);
  tbl->ents = ents;
  tbl->cap_entries = new_cap;
  tbl->first_ent = 0;
}

void grpc_chttp2_hptbl_set_max_bytes(grpc_chttp2_hptbl* tbl,
                                     uint32_t max_bytes) {
  if (tbl->max_bytes == max_bytes) {
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "Update hpack parser max size to %d", max_bytes);
  }
  while (tbl->mem_used > max_bytes) {
    evict1(tbl);
  }
  tbl->max_bytes = max_bytes;
}

grpc_error_handle grpc_chttp2_hptbl_set_current_table_size(
    grpc_chttp2_hptbl* tbl, uint32_t bytes) {
  if (tbl->current_table_bytes == bytes) {
    return GRPC_ERROR_NONE;
  }
  if (bytes > tbl->max_bytes) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat(
            "Attempt to make hpack table %d bytes when max is %d bytes", bytes,
            tbl->max_bytes)
            .c_str());
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_http_trace)) {
    gpr_log(GPR_INFO, "Update hpack parser table size to %d", bytes);
  }
  while (tbl->mem_used > bytes) {
    evict1(tbl);
  }
  tbl->current_table_bytes = bytes;
  tbl->max_entries = grpc_chttp2_hptbl::entries_for_bytes(bytes);
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

grpc_error_handle grpc_chttp2_hptbl_add(grpc_chttp2_hptbl* tbl,
                                        grpc_mdelem md) {
  /* determine how many bytes of buffer this entry represents */
  size_t elem_bytes = GRPC_SLICE_LENGTH(GRPC_MDKEY(md)) +
                      GRPC_SLICE_LENGTH(GRPC_MDVALUE(md)) +
                      GRPC_CHTTP2_HPACK_ENTRY_OVERHEAD;

  if (tbl->current_table_bytes > tbl->max_bytes) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrFormat(
            "HPACK max table size reduced to %d but not reflected by hpack "
            "stream (still at %d)",
            tbl->max_bytes, tbl->current_table_bytes)
            .c_str());
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
      evict1(tbl);
    }
    return GRPC_ERROR_NONE;
  }

  /* evict entries to ensure no overflow */
  while (elem_bytes >
         static_cast<size_t>(tbl->current_table_bytes) - tbl->mem_used) {
    evict1(tbl);
  }

  /* copy the finalized entry in */
  tbl->ents[(tbl->first_ent + tbl->num_ents) % tbl->cap_entries] =
      GRPC_MDELEM_REF(md);

  /* update accounting values */
  tbl->num_ents++;
  tbl->mem_used += static_cast<uint32_t>(elem_bytes);
  return GRPC_ERROR_NONE;
}

grpc_chttp2_hptbl_find_result grpc_chttp2_hptbl_find(
    const grpc_chttp2_hptbl* tbl, grpc_mdelem md) {
  grpc_chttp2_hptbl_find_result r = {0, 0};
  uint32_t i;

  /* See if the string is in the static table */
  for (i = 0; i < GRPC_CHTTP2_LAST_STATIC_ENTRY; i++) {
    grpc_mdelem ent = grpc_static_mdelem_manifested()[i];
    if (!grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDKEY(ent))) continue;
    r.index = i + 1u;
    r.has_value = grpc_slice_eq(GRPC_MDVALUE(md), GRPC_MDVALUE(ent));
    if (r.has_value) return r;
  }

  /* Scan the dynamic table */
  for (i = 0; i < tbl->num_ents; i++) {
    uint32_t idx = static_cast<uint32_t>(tbl->num_ents - i +
                                         GRPC_CHTTP2_LAST_STATIC_ENTRY);
    grpc_mdelem ent = tbl->ents[(tbl->first_ent + i) % tbl->cap_entries];
    if (!grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDKEY(ent))) continue;
    r.index = idx;
    r.has_value = grpc_slice_eq(GRPC_MDVALUE(md), GRPC_MDVALUE(ent));
    if (r.has_value) return r;
  }

  return r;
}

static size_t get_base64_encoded_size(size_t raw_length) {
  static const uint8_t tail_xtra[3] = {0, 2, 3};
  return raw_length / 3 * 4 + tail_xtra[raw_length % 3];
}

size_t grpc_chttp2_get_size_in_hpack_table(grpc_mdelem elem,
                                           bool use_true_binary_metadata) {
  const uint8_t* key_buf = GRPC_SLICE_START_PTR(GRPC_MDKEY(elem));
  size_t key_len = GRPC_SLICE_LENGTH(GRPC_MDKEY(elem));
  size_t overhead_and_key = 32 + key_len;
  size_t value_len = GRPC_SLICE_LENGTH(GRPC_MDVALUE(elem));
  if (grpc_key_is_binary_header(key_buf, key_len)) {
    return overhead_and_key + (use_true_binary_metadata
                                   ? value_len + 1
                                   : get_base64_encoded_size(value_len));
  } else {
    return overhead_and_key + value_len;
  }
}
