//
// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "src/core/ext/client_config/method_config.h"

#include <string.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/transport/metadata.h"

//
// grpc_method_config
//

struct grpc_method_config {
  gpr_refcount refs;
  bool* wait_for_ready;
  gpr_timespec* timeout;
  int32_t* max_request_message_bytes;
  int32_t* max_response_message_bytes;
};

grpc_method_config* grpc_method_config_create(
    bool* wait_for_ready, gpr_timespec* timeout,
    int32_t* max_request_message_bytes, int32_t* max_response_message_bytes) {
  grpc_method_config* config = gpr_malloc(sizeof(*config));
  memset(config, 0, sizeof(*config));
  gpr_ref_init(&config->refs, 1);
  if (wait_for_ready != NULL) {
    config->wait_for_ready = gpr_malloc(sizeof(*wait_for_ready));
    *config->wait_for_ready = *wait_for_ready;
  }
  if (timeout != NULL) {
    config->timeout = gpr_malloc(sizeof(*timeout));
    *config->timeout = *timeout;
  }
  if (max_request_message_bytes != NULL) {
    config->max_request_message_bytes =
        gpr_malloc(sizeof(*max_request_message_bytes));
    *config->max_request_message_bytes = *max_request_message_bytes;
  }
  if (max_response_message_bytes != NULL) {
    config->max_response_message_bytes =
        gpr_malloc(sizeof(*max_response_message_bytes));
    *config->max_response_message_bytes = *max_response_message_bytes;
  }
  return config;
}

grpc_method_config* grpc_method_config_ref(grpc_method_config* method_config) {
  gpr_ref(&method_config->refs);
  return method_config;
}

void grpc_method_config_unref(grpc_method_config* method_config) {
  if (gpr_unref(&method_config->refs)) {
    gpr_free(method_config->wait_for_ready);
    gpr_free(method_config->timeout);
    gpr_free(method_config->max_request_message_bytes);
    gpr_free(method_config->max_response_message_bytes);
    gpr_free(method_config);
  }
}

bool* grpc_method_config_get_wait_for_ready(grpc_method_config* method_config) {
  return method_config->wait_for_ready;
}

gpr_timespec* grpc_method_config_get_timeout(
    grpc_method_config* method_config) {
  return method_config->timeout;
}

int32_t* grpc_method_config_get_max_request_message_bytes(
    grpc_method_config* method_config) {
  return method_config->max_request_message_bytes;
}

int32_t* grpc_method_config_get_max_response_message_bytes(
    grpc_method_config* method_config) {
  return method_config->max_response_message_bytes;
}

//
// grpc_method_config_table
//

typedef struct grpc_method_config_table_entry {
  grpc_mdstr* path;
  grpc_method_config* method_config;
} grpc_method_config_table_entry;

#define METHOD_CONFIG_TABLE_SIZE 128
struct grpc_method_config_table {
  gpr_refcount refs;
  grpc_method_config_table_entry entries[METHOD_CONFIG_TABLE_SIZE];
};

grpc_method_config_table* grpc_method_config_table_create() {
  grpc_method_config_table* table = gpr_malloc(sizeof(*table));
  memset(table, 0, sizeof(*table));
  gpr_ref_init(&table->refs, 1);
  return table;
}

grpc_method_config_table* grpc_method_config_table_ref(
    grpc_method_config_table* table) {
  if (table != NULL) gpr_ref(&table->refs);
  return table;
}

void grpc_method_config_table_unref(grpc_method_config_table* table) {
  if (table != NULL && gpr_unref(&table->refs)) {
    for (size_t i = 0; i < GPR_ARRAY_SIZE(table->entries); ++i) {
      grpc_method_config_table_entry* entry = &table->entries[i];
      if (entry->path != NULL) {
        GRPC_MDSTR_UNREF(entry->path);
        grpc_method_config_unref(entry->method_config);
      }
    }
  }
}

// Helper function for insert and get operations that performs quadratic
// probing (https://en.wikipedia.org/wiki/Quadratic_probing).
static size_t grpc_method_config_table_find_index(
    grpc_method_config_table* table, grpc_mdstr* path, bool find_empty) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(table->entries); ++i) {
    const size_t idx = (path->hash + i * i) % GPR_ARRAY_SIZE(table->entries);
    if (table->entries[idx].path == NULL)
      return find_empty ? idx : GPR_ARRAY_SIZE(table->entries);
    if (table->entries[idx].path == path) return idx;
  }
  return GPR_ARRAY_SIZE(table->entries) + 1;  // Not found.
}

static void grpc_method_config_table_insert(grpc_method_config_table* table,
                                            grpc_mdstr* path,
                                            grpc_method_config* config) {
  const size_t idx =
      grpc_method_config_table_find_index(table, path, true /* find_empty */);
  // This can happen if the table is full.
  GPR_ASSERT(idx != GPR_ARRAY_SIZE(table->entries));
  grpc_method_config_table_entry* entry = &table->entries[idx];
  entry->path = GRPC_MDSTR_REF(path);
  entry->method_config = grpc_method_config_ref(config);
}

static grpc_method_config* grpc_method_config_table_get(
    grpc_method_config_table* table, grpc_mdstr* path) {
  const size_t idx =
      grpc_method_config_table_find_index(table, path, false /* find_empty */);
  if (idx == GPR_ARRAY_SIZE(table->entries)) return NULL;  // Not found.
  return table->entries[idx].method_config;
}

void grpc_method_config_table_add_method_config(
    grpc_method_config_table* table, grpc_mdstr** paths, size_t num_paths,
    grpc_method_config* method_config) {
  for (size_t i = 0; i < num_paths; ++i) {
    grpc_method_config_table_insert(table, paths[i], method_config);
  }
}

grpc_method_config* grpc_method_config_table_get_method_config(
    grpc_method_config_table* table, grpc_mdstr* path) {
  grpc_method_config* method_config = grpc_method_config_table_get(table, path);
  // If we didn't find a match for the path, try looking for a wildcard
  // entry (i.e., change "/service/method" to "/service/*").
  if (method_config == NULL) {
    const char* path_str = grpc_mdstr_as_c_string(path);
    const char* sep = strrchr(path_str, '/') + 1;
    const size_t len = (size_t)(sep - path_str);
    char buf[len + 2];  // '*' and NUL
    memcpy(buf, path_str, len);
    buf[len] = '*';
    buf[len + 1] = '\0';
    grpc_mdstr* wildcard_path = grpc_mdstr_from_string(buf);
    method_config = grpc_method_config_table_get(table, wildcard_path);
    GRPC_MDSTR_UNREF(wildcard_path);
  }
  return grpc_method_config_ref(method_config);
}

static void* copy_arg(void* p) {
  return grpc_method_config_table_ref(p);
}

static void destroy_arg(void* p) {
  grpc_method_config_table_unref(p);
}

static int cmp_arg(void* p1, void* p2) {
  grpc_method_config_table* t1 = p1;
  grpc_method_config_table* t2 = p2;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(t1->entries); ++i) {
    grpc_method_config_table_entry* e1 = &t1->entries[i];
    grpc_method_config_table_entry* e2 = &t2->entries[i];
    // Compare paths by hash value.
    if (e1->path->hash < e2->path->hash) return -1;
    if (e1->path->hash > e2->path->hash) return 1;
    // Compare wait_for_ready.
    const bool wait_for_ready1 =
        e1->method_config->wait_for_ready == NULL
        ? false : *e1->method_config->wait_for_ready;
    const bool wait_for_ready2 =
        e2->method_config->wait_for_ready == NULL
        ? false : *e2->method_config->wait_for_ready;
    if (wait_for_ready1 < wait_for_ready2) return -1;
    if (wait_for_ready1 > wait_for_ready2) return 1;
    // Compare timeout.
    const gpr_timespec timeout1 =
        e1->method_config->timeout == NULL
        ? gpr_inf_past(GPR_CLOCK_MONOTONIC) : *e1->method_config->timeout;
    const gpr_timespec timeout2 =
        e2->method_config->timeout == NULL
        ? gpr_inf_past(GPR_CLOCK_MONOTONIC) : *e2->method_config->timeout;
    const int timeout_result = gpr_time_cmp(timeout1, timeout2);
    if (timeout_result != 0) return timeout_result;
    // Compare max_request_message_bytes.
    const int32_t max_request_message_bytes1 =
        e1->method_config->max_request_message_bytes == NULL
        ? -1 : *e1->method_config->max_request_message_bytes;
    const int32_t max_request_message_bytes2 =
        e2->method_config->max_request_message_bytes == NULL
        ? -1 : *e2->method_config->max_request_message_bytes;
    if (max_request_message_bytes1 < max_request_message_bytes2) return -1;
    if (max_request_message_bytes1 > max_request_message_bytes2) return 1;
    // Compare max_response_message_bytes.
    const int32_t max_response_message_bytes1 =
        e1->method_config->max_response_message_bytes == NULL
        ? -1 : *e1->method_config->max_response_message_bytes;
    const int32_t max_response_message_bytes2 =
        e2->method_config->max_response_message_bytes == NULL
        ? -1 : *e2->method_config->max_response_message_bytes;
    if (max_response_message_bytes1 < max_response_message_bytes2) return -1;
    if (max_response_message_bytes1 > max_response_message_bytes2) return 1;
  }
  return 0;
}

static grpc_arg_pointer_vtable arg_vtable = {copy_arg, destroy_arg, cmp_arg};

grpc_arg grpc_method_config_table_create_channel_arg(
    grpc_method_config_table* table) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_SERVICE_CONFIG;
  arg.value.pointer.p = grpc_method_config_table_ref(table);
  arg.value.pointer.vtable = &arg_vtable;
  return arg;
}
