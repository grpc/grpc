/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/filters/client_channel/retry_throttle.h"

#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/avl.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

//
// server_retry_throttle_data
//

struct grpc_server_retry_throttle_data {
  gpr_refcount refs;
  int max_milli_tokens;
  int milli_token_ratio;
  gpr_atm milli_tokens;
  // A pointer to the replacement for this grpc_server_retry_throttle_data
  // entry.  If non-NULL, then this entry is stale and must not be used.
  // We hold a reference to the replacement.
  gpr_atm replacement;
};

static void get_replacement_throttle_data_if_needed(
    grpc_server_retry_throttle_data** throttle_data) {
  while (true) {
    grpc_server_retry_throttle_data* new_throttle_data =
        (grpc_server_retry_throttle_data*)gpr_atm_acq_load(
            &(*throttle_data)->replacement);
    if (new_throttle_data == NULL) return;
    *throttle_data = new_throttle_data;
  }
}

bool grpc_server_retry_throttle_data_record_failure(
    grpc_server_retry_throttle_data* throttle_data) {
  // First, check if we are stale and need to be replaced.
  get_replacement_throttle_data_if_needed(&throttle_data);
  // We decrement milli_tokens by 1000 (1 token) for each failure.
  const int new_value = (int)gpr_atm_no_barrier_clamped_add(
      &throttle_data->milli_tokens, (gpr_atm)-1000, (gpr_atm)0,
      (gpr_atm)throttle_data->max_milli_tokens);
  // Retries are allowed as long as the new value is above the threshold
  // (max_milli_tokens / 2).
  return new_value > throttle_data->max_milli_tokens / 2;
}

void grpc_server_retry_throttle_data_record_success(
    grpc_server_retry_throttle_data* throttle_data) {
  // First, check if we are stale and need to be replaced.
  get_replacement_throttle_data_if_needed(&throttle_data);
  // We increment milli_tokens by milli_token_ratio for each success.
  gpr_atm_no_barrier_clamped_add(
      &throttle_data->milli_tokens, (gpr_atm)throttle_data->milli_token_ratio,
      (gpr_atm)0, (gpr_atm)throttle_data->max_milli_tokens);
}

grpc_server_retry_throttle_data* grpc_server_retry_throttle_data_ref(
    grpc_server_retry_throttle_data* throttle_data) {
  gpr_ref(&throttle_data->refs);
  return throttle_data;
}

void grpc_server_retry_throttle_data_unref(
    grpc_server_retry_throttle_data* throttle_data) {
  if (gpr_unref(&throttle_data->refs)) {
    grpc_server_retry_throttle_data* replacement =
        (grpc_server_retry_throttle_data*)gpr_atm_acq_load(
            &throttle_data->replacement);
    if (replacement != NULL) {
      grpc_server_retry_throttle_data_unref(replacement);
    }
    gpr_free(throttle_data);
  }
}

static grpc_server_retry_throttle_data* grpc_server_retry_throttle_data_create(
    int max_milli_tokens, int milli_token_ratio,
    grpc_server_retry_throttle_data* old_throttle_data) {
  grpc_server_retry_throttle_data* throttle_data =
      gpr_malloc(sizeof(*throttle_data));
  memset(throttle_data, 0, sizeof(*throttle_data));
  gpr_ref_init(&throttle_data->refs, 1);
  throttle_data->max_milli_tokens = max_milli_tokens;
  throttle_data->milli_token_ratio = milli_token_ratio;
  int initial_milli_tokens = max_milli_tokens;
  // If there was a pre-existing entry for this server name, initialize
  // the token count by scaling proportionately to the old data.  This
  // ensures that if we're already throttling retries on the old scale,
  // we will start out doing the same thing on the new one.
  if (old_throttle_data != NULL) {
    double token_fraction =
        (int)gpr_atm_acq_load(&old_throttle_data->milli_tokens) /
        (double)old_throttle_data->max_milli_tokens;
    initial_milli_tokens = (int)(token_fraction * max_milli_tokens);
  }
  gpr_atm_rel_store(&throttle_data->milli_tokens,
                    (gpr_atm)initial_milli_tokens);
  // If there was a pre-existing entry, mark it as stale and give it a
  // pointer to the new entry, which is its replacement.
  if (old_throttle_data != NULL) {
    grpc_server_retry_throttle_data_ref(throttle_data);
    gpr_atm_rel_store(&old_throttle_data->replacement, (gpr_atm)throttle_data);
  }
  return throttle_data;
}

//
// avl vtable for string -> server_retry_throttle_data map
//

static void* copy_server_name(void* key, void* unused) {
  return gpr_strdup(key);
}

static long compare_server_name(void* key1, void* key2, void* unused) {
  return strcmp(key1, key2);
}

static void destroy_server_retry_throttle_data(void* value, void* unused) {
  grpc_server_retry_throttle_data* throttle_data = value;
  grpc_server_retry_throttle_data_unref(throttle_data);
}

static void* copy_server_retry_throttle_data(void* value, void* unused) {
  grpc_server_retry_throttle_data* throttle_data = value;
  return grpc_server_retry_throttle_data_ref(throttle_data);
}

static void destroy_server_name(void* key, void* unused) { gpr_free(key); }

static const gpr_avl_vtable avl_vtable = {
    destroy_server_name, copy_server_name, compare_server_name,
    destroy_server_retry_throttle_data, copy_server_retry_throttle_data};

//
// server_retry_throttle_map
//

static gpr_mu g_mu;
static gpr_avl g_avl;

void grpc_retry_throttle_map_init() {
  gpr_mu_init(&g_mu);
  g_avl = gpr_avl_create(&avl_vtable);
}

void grpc_retry_throttle_map_shutdown() {
  gpr_mu_destroy(&g_mu);
  gpr_avl_unref(g_avl, NULL);
}

grpc_server_retry_throttle_data* grpc_retry_throttle_map_get_data_for_server(
    const char* server_name, int max_milli_tokens, int milli_token_ratio) {
  gpr_mu_lock(&g_mu);
  grpc_server_retry_throttle_data* throttle_data =
      gpr_avl_get(g_avl, (char*)server_name, NULL);
  if (throttle_data == NULL) {
    // Entry not found.  Create a new one.
    throttle_data = grpc_server_retry_throttle_data_create(
        max_milli_tokens, milli_token_ratio, NULL);
    g_avl = gpr_avl_add(g_avl, (char*)server_name, throttle_data, NULL);
  } else {
    if (throttle_data->max_milli_tokens != max_milli_tokens ||
        throttle_data->milli_token_ratio != milli_token_ratio) {
      // Entry found but with old parameters.  Create a new one based on
      // the original one.
      throttle_data = grpc_server_retry_throttle_data_create(
          max_milli_tokens, milli_token_ratio, throttle_data);
      g_avl = gpr_avl_add(g_avl, (char*)server_name, throttle_data, NULL);
    } else {
      // Entry found.  Increase refcount.
      grpc_server_retry_throttle_data_ref(throttle_data);
    }
  }
  gpr_mu_unlock(&g_mu);
  return throttle_data;
}
