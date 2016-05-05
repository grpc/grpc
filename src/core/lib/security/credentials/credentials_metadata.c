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

#include "src/core/lib/security/credentials/credentials.h"

#include <grpc/support/alloc.h>

#include <string.h>

static void store_ensure_capacity(grpc_credentials_md_store *store) {
  if (store->num_entries == store->allocated) {
    store->allocated = (store->allocated == 0) ? 1 : store->allocated * 2;
    store->entries = gpr_realloc(
        store->entries, store->allocated * sizeof(grpc_credentials_md));
  }
}

grpc_credentials_md_store *grpc_credentials_md_store_create(
    size_t initial_capacity) {
  grpc_credentials_md_store *store =
      gpr_malloc(sizeof(grpc_credentials_md_store));
  memset(store, 0, sizeof(grpc_credentials_md_store));
  if (initial_capacity > 0) {
    store->entries = gpr_malloc(initial_capacity * sizeof(grpc_credentials_md));
    store->allocated = initial_capacity;
  }
  gpr_ref_init(&store->refcount, 1);
  return store;
}

void grpc_credentials_md_store_add(grpc_credentials_md_store *store,
                                   gpr_slice key, gpr_slice value) {
  if (store == NULL) return;
  store_ensure_capacity(store);
  store->entries[store->num_entries].key = gpr_slice_ref(key);
  store->entries[store->num_entries].value = gpr_slice_ref(value);
  store->num_entries++;
}

void grpc_credentials_md_store_add_cstrings(grpc_credentials_md_store *store,
                                            const char *key,
                                            const char *value) {
  if (store == NULL) return;
  store_ensure_capacity(store);
  store->entries[store->num_entries].key = gpr_slice_from_copied_string(key);
  store->entries[store->num_entries].value =
      gpr_slice_from_copied_string(value);
  store->num_entries++;
}

grpc_credentials_md_store *grpc_credentials_md_store_ref(
    grpc_credentials_md_store *store) {
  if (store == NULL) return NULL;
  gpr_ref(&store->refcount);
  return store;
}

void grpc_credentials_md_store_unref(grpc_credentials_md_store *store) {
  if (store == NULL) return;
  if (gpr_unref(&store->refcount)) {
    if (store->entries != NULL) {
      size_t i;
      for (i = 0; i < store->num_entries; i++) {
        gpr_slice_unref(store->entries[i].key);
        gpr_slice_unref(store->entries[i].value);
      }
      gpr_free(store->entries);
    }
    gpr_free(store);
  }
}
