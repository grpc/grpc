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

#include "src/core/ext/census/census_tracing.h"
#include "src/core/ext/census/census_interface.h"

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include "src/core/ext/census/hash_table.h"
#include "src/core/lib/support/string.h"

void census_trace_obj_destroy(census_trace_obj *obj) {
  census_trace_annotation *p = obj->annotations;
  while (p != NULL) {
    census_trace_annotation *next = p->next;
    gpr_free(p);
    p = next;
  }
  gpr_free(obj->method);
  gpr_free(obj);
}

static void delete_trace_obj(void *obj) {
  census_trace_obj_destroy((census_trace_obj *)obj);
}

static const census_ht_option ht_opt = {
    CENSUS_HT_UINT64 /* key type */,
    571 /* n_of_buckets */,
    NULL /* hash */,
    NULL /* compare_keys */,
    delete_trace_obj /* delete data */,
    NULL /* delete key */
};

static gpr_once g_init_mutex_once = GPR_ONCE_INIT;
static gpr_mu g_mu; /* Guards following two static variables. */
static census_ht *g_trace_store = NULL;
static uint64_t g_id = 0;

static census_ht_key op_id_as_key(census_op_id *id) {
  return *(census_ht_key *)id;
}

static uint64_t op_id_2_uint64(census_op_id *id) {
  uint64_t ret;
  memcpy(&ret, id, sizeof(census_op_id));
  return ret;
}

static void init_mutex(void) { gpr_mu_init(&g_mu); }

static void init_mutex_once(void) {
  gpr_once_init(&g_init_mutex_once, init_mutex);
}

census_op_id census_tracing_start_op(void) {
  gpr_mu_lock(&g_mu);
  {
    census_trace_obj *ret = gpr_malloc(sizeof(census_trace_obj));
    memset(ret, 0, sizeof(census_trace_obj));
    g_id++;
    memcpy(&ret->id, &g_id, sizeof(census_op_id));
    ret->rpc_stats.cnt = 1;
    ret->ts = gpr_now(GPR_CLOCK_REALTIME);
    census_ht_insert(g_trace_store, op_id_as_key(&ret->id), (void *)ret);
    gpr_log(GPR_DEBUG, "Start tracing for id %lu", g_id);
    gpr_mu_unlock(&g_mu);
    return ret->id;
  }
}

int census_add_method_tag(census_op_id op_id, const char *method) {
  int ret = 0;
  census_trace_obj *trace = NULL;
  gpr_mu_lock(&g_mu);
  trace = census_ht_find(g_trace_store, op_id_as_key(&op_id));
  if (trace == NULL) {
    ret = 1;
  } else {
    trace->method = gpr_strdup(method);
  }
  gpr_mu_unlock(&g_mu);
  return ret;
}

void census_tracing_print(census_op_id op_id, const char *anno_txt) {
  census_trace_obj *trace = NULL;
  gpr_mu_lock(&g_mu);
  trace = census_ht_find(g_trace_store, op_id_as_key(&op_id));
  if (trace != NULL) {
    census_trace_annotation *anno = gpr_malloc(sizeof(census_trace_annotation));
    anno->ts = gpr_now(GPR_CLOCK_REALTIME);
    {
      char *d = anno->txt;
      const char *s = anno_txt;
      int n = 0;
      for (; n < CENSUS_MAX_ANNOTATION_LENGTH && *s != '\0'; ++n) {
        *d++ = *s++;
      }
      *d = '\0';
    }
    anno->next = trace->annotations;
    trace->annotations = anno;
  }
  gpr_mu_unlock(&g_mu);
}

void census_tracing_end_op(census_op_id op_id) {
  census_trace_obj *trace = NULL;
  gpr_mu_lock(&g_mu);
  trace = census_ht_find(g_trace_store, op_id_as_key(&op_id));
  if (trace != NULL) {
    trace->rpc_stats.elapsed_time_ms = gpr_timespec_to_micros(
        gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME), trace->ts));
    gpr_log(GPR_DEBUG, "End tracing for id %lu, method %s, latency %f us",
            op_id_2_uint64(&op_id), trace->method,
            trace->rpc_stats.elapsed_time_ms);
    census_ht_erase(g_trace_store, op_id_as_key(&op_id));
  }
  gpr_mu_unlock(&g_mu);
}

void census_tracing_init(void) {
  init_mutex_once();
  gpr_mu_lock(&g_mu);
  if (g_trace_store == NULL) {
    g_id = 1;
    g_trace_store = census_ht_create(&ht_opt);
  } else {
    gpr_log(GPR_ERROR, "Census trace store already initialized.");
  }
  gpr_mu_unlock(&g_mu);
}

void census_tracing_shutdown(void) {
  gpr_mu_lock(&g_mu);
  if (g_trace_store != NULL) {
    census_ht_destroy(g_trace_store);
    g_trace_store = NULL;
  } else {
    gpr_log(GPR_ERROR, "Census trace store is not initialized.");
  }
  gpr_mu_unlock(&g_mu);
}

void census_internal_lock_trace_store(void) { gpr_mu_lock(&g_mu); }

void census_internal_unlock_trace_store(void) { gpr_mu_unlock(&g_mu); }

census_trace_obj *census_get_trace_obj_locked(census_op_id op_id) {
  if (g_trace_store == NULL) {
    gpr_log(GPR_ERROR, "Census trace store is not initialized.");
    return NULL;
  }
  return (census_trace_obj *)census_ht_find(g_trace_store,
                                            op_id_as_key(&op_id));
}

const char *census_get_trace_method_name(const census_trace_obj *trace) {
  return trace->method;
}

static census_trace_annotation *dup_annotation_chain(
    census_trace_annotation *from) {
  census_trace_annotation *ret = NULL;
  census_trace_annotation **to = &ret;
  for (; from != NULL; from = from->next) {
    *to = gpr_malloc(sizeof(census_trace_annotation));
    memcpy(*to, from, sizeof(census_trace_annotation));
    to = &(*to)->next;
  }
  return ret;
}

static census_trace_obj *trace_obj_dup(census_trace_obj *from) {
  census_trace_obj *to = NULL;
  GPR_ASSERT(from != NULL);
  to = gpr_malloc(sizeof(census_trace_obj));
  to->id = from->id;
  to->ts = from->ts;
  to->rpc_stats = from->rpc_stats;
  to->method = gpr_strdup(from->method);
  to->annotations = dup_annotation_chain(from->annotations);
  return to;
}

census_trace_obj **census_get_active_ops(int *num_active_ops) {
  census_trace_obj **ret = NULL;
  gpr_mu_lock(&g_mu);
  if (g_trace_store != NULL) {
    size_t n = 0;
    census_ht_kv *all_kvs = census_ht_get_all_elements(g_trace_store, &n);
    *num_active_ops = (int)n;
    if (n != 0) {
      size_t i = 0;
      ret = gpr_malloc(sizeof(census_trace_obj *) * n);
      for (i = 0; i < n; i++) {
        ret[i] = trace_obj_dup((census_trace_obj *)all_kvs[i].v);
      }
    }
    gpr_free(all_kvs);
  }
  gpr_mu_unlock(&g_mu);
  return ret;
}
