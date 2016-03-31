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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include "src/core/ext/census/census_interface.h"
#include "src/core/ext/census/census_rpc_stats.h"
#include "src/core/ext/census/census_tracing.h"
#include "src/core/ext/census/hash_table.h"
#include "src/core/ext/census/window_stats.h"
#include "src/core/lib/support/murmur_hash.h"
#include "src/core/lib/support/string.h"

#define NUM_INTERVALS 3
#define MINUTE_INTERVAL 0
#define HOUR_INTERVAL 1
#define TOTAL_INTERVAL 2

/* for easier typing */
typedef census_per_method_rpc_stats per_method_stats;

/* Ensure mu is only initialized once. */
static gpr_once g_stats_store_mu_init = GPR_ONCE_INIT;
/* Guards two stats stores. */
static gpr_mu g_mu;
static census_ht *g_client_stats_store = NULL;
static census_ht *g_server_stats_store = NULL;

static void init_mutex(void) { gpr_mu_init(&g_mu); }

static void init_mutex_once(void) {
  gpr_once_init(&g_stats_store_mu_init, init_mutex);
}

static int cmp_str_keys(const void *k1, const void *k2) {
  return strcmp((const char *)k1, (const char *)k2);
}

/* TODO(hongyu): replace it with cityhash64 */
static uint64_t simple_hash(const void *k) {
  size_t len = strlen(k);
  uint64_t higher = gpr_murmur_hash3((const char *)k, len / 2, 0);
  return higher << 32 |
         gpr_murmur_hash3((const char *)k + len / 2, len - len / 2, 0);
}

static void delete_stats(void *stats) {
  census_window_stats_destroy((struct census_window_stats *)stats);
}

static void delete_key(void *key) { gpr_free(key); }

static const census_ht_option ht_opt = {
    CENSUS_HT_POINTER /* key type */, 1999 /* n_of_buckets */,
    simple_hash /* hash function */,  cmp_str_keys /* key comparator */,
    delete_stats /* data deleter */,  delete_key /* key deleter */
};

static void init_rpc_stats(void *stats) {
  memset(stats, 0, sizeof(census_rpc_stats));
}

static void stat_add_proportion(double p, void *base, const void *addme) {
  census_rpc_stats *b = (census_rpc_stats *)base;
  census_rpc_stats *a = (census_rpc_stats *)addme;
  b->cnt += p * a->cnt;
  b->rpc_error_cnt += p * a->rpc_error_cnt;
  b->app_error_cnt += p * a->app_error_cnt;
  b->elapsed_time_ms += p * a->elapsed_time_ms;
  b->api_request_bytes += p * a->api_request_bytes;
  b->wire_request_bytes += p * a->wire_request_bytes;
  b->api_response_bytes += p * a->api_response_bytes;
  b->wire_response_bytes += p * a->wire_response_bytes;
}

static void stat_add(void *base, const void *addme) {
  stat_add_proportion(1.0, base, addme);
}

static gpr_timespec min_hour_total_intervals[3] = {
    {60, 0}, {3600, 0}, {36000000, 0}};

static const census_window_stats_stat_info window_stats_settings = {
    sizeof(census_rpc_stats), init_rpc_stats, stat_add, stat_add_proportion};

census_rpc_stats *census_rpc_stats_create_empty(void) {
  census_rpc_stats *ret =
      (census_rpc_stats *)gpr_malloc(sizeof(census_rpc_stats));
  memset(ret, 0, sizeof(census_rpc_stats));
  return ret;
}

void census_aggregated_rpc_stats_set_empty(census_aggregated_rpc_stats *data) {
  int i = 0;
  for (i = 0; i < data->num_entries; i++) {
    if (data->stats[i].method != NULL) {
      gpr_free((void *)data->stats[i].method);
    }
  }
  if (data->stats != NULL) {
    gpr_free(data->stats);
  }
  data->num_entries = 0;
  data->stats = NULL;
}

static void record_stats(census_ht *store, census_op_id op_id,
                         const census_rpc_stats *stats) {
  gpr_mu_lock(&g_mu);
  if (store != NULL) {
    census_trace_obj *trace = NULL;
    census_internal_lock_trace_store();
    trace = census_get_trace_obj_locked(op_id);
    if (trace != NULL) {
      const char *method_name = census_get_trace_method_name(trace);
      struct census_window_stats *window_stats = NULL;
      census_ht_key key;
      key.ptr = (void *)method_name;
      window_stats = census_ht_find(store, key);
      census_internal_unlock_trace_store();
      if (window_stats == NULL) {
        window_stats = census_window_stats_create(3, min_hour_total_intervals,
                                                  30, &window_stats_settings);
        key.ptr = gpr_strdup(key.ptr);
        census_ht_insert(store, key, (void *)window_stats);
      }
      census_window_stats_add(window_stats, gpr_now(GPR_CLOCK_REALTIME), stats);
    } else {
      census_internal_unlock_trace_store();
    }
  }
  gpr_mu_unlock(&g_mu);
}

void census_record_rpc_client_stats(census_op_id op_id,
                                    const census_rpc_stats *stats) {
  record_stats(g_client_stats_store, op_id, stats);
}

void census_record_rpc_server_stats(census_op_id op_id,
                                    const census_rpc_stats *stats) {
  record_stats(g_server_stats_store, op_id, stats);
}

/* Get stats from input stats store */
static void get_stats(census_ht *store, census_aggregated_rpc_stats *data) {
  GPR_ASSERT(data != NULL);
  if (data->num_entries != 0) {
    census_aggregated_rpc_stats_set_empty(data);
  }
  gpr_mu_lock(&g_mu);
  if (store != NULL) {
    size_t n;
    unsigned i, j;
    gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
    census_ht_kv *kv = census_ht_get_all_elements(store, &n);
    if (kv != NULL) {
      data->num_entries = n;
      data->stats =
          (per_method_stats *)gpr_malloc(sizeof(per_method_stats) * n);
      for (i = 0; i < n; i++) {
        census_window_stats_sums sums[NUM_INTERVALS];
        for (j = 0; j < NUM_INTERVALS; j++) {
          sums[j].statistic = (void *)census_rpc_stats_create_empty();
        }
        data->stats[i].method = gpr_strdup(kv[i].k.ptr);
        census_window_stats_get_sums(kv[i].v, now, sums);
        data->stats[i].minute_stats =
            *(census_rpc_stats *)sums[MINUTE_INTERVAL].statistic;
        data->stats[i].hour_stats =
            *(census_rpc_stats *)sums[HOUR_INTERVAL].statistic;
        data->stats[i].total_stats =
            *(census_rpc_stats *)sums[TOTAL_INTERVAL].statistic;
        for (j = 0; j < NUM_INTERVALS; j++) {
          gpr_free(sums[j].statistic);
        }
      }
      gpr_free(kv);
    }
  }
  gpr_mu_unlock(&g_mu);
}

void census_get_client_stats(census_aggregated_rpc_stats *data) {
  get_stats(g_client_stats_store, data);
}

void census_get_server_stats(census_aggregated_rpc_stats *data) {
  get_stats(g_server_stats_store, data);
}

void census_stats_store_init(void) {
  init_mutex_once();
  gpr_mu_lock(&g_mu);
  if (g_client_stats_store == NULL && g_server_stats_store == NULL) {
    g_client_stats_store = census_ht_create(&ht_opt);
    g_server_stats_store = census_ht_create(&ht_opt);
  } else {
    gpr_log(GPR_ERROR, "Census stats store already initialized.");
  }
  gpr_mu_unlock(&g_mu);
}

void census_stats_store_shutdown(void) {
  init_mutex_once();
  gpr_mu_lock(&g_mu);
  if (g_client_stats_store != NULL) {
    census_ht_destroy(g_client_stats_store);
    g_client_stats_store = NULL;
  } else {
    gpr_log(GPR_ERROR, "Census server stats store not initialized.");
  }
  if (g_server_stats_store != NULL) {
    census_ht_destroy(g_server_stats_store);
    g_server_stats_store = NULL;
  } else {
    gpr_log(GPR_ERROR, "Census client stats store not initialized.");
  }
  gpr_mu_unlock(&g_mu);
}
