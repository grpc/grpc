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
