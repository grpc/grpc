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

#include "src/core/ext/client_config/resolver_result.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/channel/channel_args.h"

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
// method_config_table
//

typedef struct method_config_table_entry {
  grpc_mdstr* path;
  grpc_method_config* method_config;
} method_config_table_entry;

#define METHOD_CONFIG_TABLE_SIZE 128
typedef struct method_config_table {
  method_config_table_entry entries[METHOD_CONFIG_TABLE_SIZE];
} method_config_table;

static void method_config_table_init(method_config_table* table) {
  memset(table, 0, sizeof(*table));
}

static void method_config_table_destroy(method_config_table* table) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(table->entries); ++i) {
    method_config_table_entry* entry = &table->entries[i];
    if (entry->path != NULL) {
      GRPC_MDSTR_UNREF(entry->path);
      grpc_method_config_unref(entry->method_config);
    }
  }
}

// Helper function for insert and get operations that performs quadratic
// probing (https://en.wikipedia.org/wiki/Quadratic_probing).
static size_t method_config_table_find_index(method_config_table* table,
                                             grpc_mdstr* path,
                                             bool find_empty) {
  for (size_t i = 0; i < GPR_ARRAY_SIZE(table->entries); ++i) {
    const size_t idx = (path->hash + i * i) % GPR_ARRAY_SIZE(table->entries);
    if (table->entries[idx].path == NULL)
      return find_empty ? idx : GPR_ARRAY_SIZE(table->entries);
    if (table->entries[idx].path == path) return idx;
  }
  return GPR_ARRAY_SIZE(table->entries) + 1;  // Not found.
}

static void method_config_table_insert(method_config_table* table,
                                       grpc_mdstr* path,
                                       grpc_method_config* config) {
  const size_t idx =
      method_config_table_find_index(table, path, true /* find_empty */);
  // This can happen if the table is full.
  GPR_ASSERT(idx != GPR_ARRAY_SIZE(table->entries));
  method_config_table_entry* entry = &table->entries[idx];
  entry->path = GRPC_MDSTR_REF(path);
  entry->method_config = grpc_method_config_ref(config);
}

static grpc_method_config* method_config_table_get(method_config_table* table,
                                                   grpc_mdstr* path) {
  const size_t idx =
      method_config_table_find_index(table, path, false /* find_empty */);
  if (idx == GPR_ARRAY_SIZE(table->entries)) return NULL;  // Not found.
  return table->entries[idx].method_config;
}

//
// grpc_resolver_result
//

struct grpc_resolver_result {
  gpr_refcount refs;
  char* server_name;
  grpc_lb_addresses* addresses;
  char* lb_policy_name;
  grpc_channel_args* lb_policy_args;
  method_config_table method_configs;
};

grpc_resolver_result* grpc_resolver_result_create(
    const char* server_name, grpc_lb_addresses* addresses,
    const char* lb_policy_name, grpc_channel_args* lb_policy_args) {
  grpc_resolver_result* result = gpr_malloc(sizeof(*result));
  memset(result, 0, sizeof(*result));
  gpr_ref_init(&result->refs, 1);
  result->server_name = gpr_strdup(server_name);
  result->addresses = addresses;
  result->lb_policy_name = gpr_strdup(lb_policy_name);
  result->lb_policy_args = lb_policy_args;
  method_config_table_init(&result->method_configs);
  return result;
}

void grpc_resolver_result_ref(grpc_resolver_result* result) {
  gpr_ref(&result->refs);
}

void grpc_resolver_result_unref(grpc_exec_ctx* exec_ctx,
                                grpc_resolver_result* result) {
  if (gpr_unref(&result->refs)) {
    gpr_free(result->server_name);
    grpc_lb_addresses_destroy(result->addresses, NULL /* user_data_destroy */);
    gpr_free(result->lb_policy_name);
    grpc_channel_args_destroy(result->lb_policy_args);
    method_config_table_destroy(&result->method_configs);
    gpr_free(result);
  }
}

const char* grpc_resolver_result_get_server_name(grpc_resolver_result* result) {
  return result->server_name;
}

grpc_lb_addresses* grpc_resolver_result_get_addresses(
    grpc_resolver_result* result) {
  return result->addresses;
}

const char* grpc_resolver_result_get_lb_policy_name(
    grpc_resolver_result* result) {
  return result->lb_policy_name;
}

grpc_channel_args* grpc_resolver_result_get_lb_policy_args(
    grpc_resolver_result* result) {
  return result->lb_policy_args;
}

void grpc_resolver_result_add_method_config(grpc_resolver_result* result,
                                            grpc_mdstr** paths,
                                            size_t num_paths,
                                            grpc_method_config* method_config) {
  for (size_t i = 0; i < num_paths; ++i) {
    method_config_table_insert(&result->method_configs, paths[i],
                               method_config);
  }
}

grpc_method_config* grpc_resolver_result_get_method_config(
    grpc_resolver_result* result, grpc_mdstr* path) {
  grpc_method_config* method_config =
      method_config_table_get(&result->method_configs, path);
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
    method_config =
        method_config_table_get(&result->method_configs, wildcard_path);
    GRPC_MDSTR_UNREF(wildcard_path);
  }
  return method_config;
}
