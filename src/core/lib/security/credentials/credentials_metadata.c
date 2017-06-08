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

#include "src/core/lib/security/credentials/credentials.h"

#include <grpc/support/alloc.h>

#include <string.h>

#include "src/core/lib/slice/slice_internal.h"

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
      gpr_zalloc(sizeof(grpc_credentials_md_store));
  if (initial_capacity > 0) {
    store->entries = gpr_malloc(initial_capacity * sizeof(grpc_credentials_md));
    store->allocated = initial_capacity;
  }
  gpr_ref_init(&store->refcount, 1);
  return store;
}

void grpc_credentials_md_store_add(grpc_credentials_md_store *store,
                                   grpc_slice key, grpc_slice value) {
  if (store == NULL) return;
  store_ensure_capacity(store);
  store->entries[store->num_entries].key = grpc_slice_ref_internal(key);
  store->entries[store->num_entries].value = grpc_slice_ref_internal(value);
  store->num_entries++;
}

void grpc_credentials_md_store_add_cstrings(grpc_credentials_md_store *store,
                                            const char *key,
                                            const char *value) {
  if (store == NULL) return;
  store_ensure_capacity(store);
  store->entries[store->num_entries].key = grpc_slice_from_copied_string(key);
  store->entries[store->num_entries].value =
      grpc_slice_from_copied_string(value);
  store->num_entries++;
}

grpc_credentials_md_store *grpc_credentials_md_store_ref(
    grpc_credentials_md_store *store) {
  if (store == NULL) return NULL;
  gpr_ref(&store->refcount);
  return store;
}

void grpc_credentials_md_store_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_credentials_md_store *store) {
  if (store == NULL) return;
  if (gpr_unref(&store->refcount)) {
    if (store->entries != NULL) {
      size_t i;
      for (i = 0; i < store->num_entries; i++) {
        grpc_slice_unref_internal(exec_ctx, store->entries[i].key);
        grpc_slice_unref_internal(exec_ctx, store->entries[i].value);
      }
      gpr_free(store->entries);
    }
    gpr_free(store);
  }
}
