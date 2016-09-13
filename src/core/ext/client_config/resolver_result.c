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

//
// grpc_addresses
//

grpc_addresses* grpc_addresses_create(size_t num_addresses) {
  grpc_addresses* addresses = gpr_malloc(sizeof(grpc_addresses));
  addresses->num_addresses = num_addresses;
  const size_t addresses_size = sizeof(grpc_address) * num_addresses;
  addresses->addresses = gpr_malloc(addresses_size);
  memset(addresses->addresses, 0, addresses_size);
  return addresses;
}

grpc_addresses* grpc_addresses_copy(grpc_addresses* addresses) {
  grpc_addresses* new = grpc_addresses_create(addresses->num_addresses);
  memcpy(new->addresses, addresses->addresses,
         sizeof(grpc_address) * addresses->num_addresses);
  return new;
}

void grpc_addresses_set_address(grpc_addresses* addresses, size_t index,
                                void* address, size_t address_len,
                                bool is_balancer) {
  GPR_ASSERT(index < addresses->num_addresses);
  grpc_address* target = &addresses->addresses[index];
  memcpy(target->address.addr, address, address_len);
  target->address.len = address_len;
  target->is_balancer = is_balancer;
}

void grpc_addresses_destroy(grpc_addresses* addresses) {
  gpr_free(addresses->addresses);
  gpr_free(addresses);
}

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
  grpc_addresses* addresses;
  char* lb_policy_name;
  method_config_table method_configs;
};

grpc_resolver_result* grpc_resolver_result_create(grpc_addresses* addresses,
                                                  const char* lb_policy_name) {
  grpc_resolver_result* result = gpr_malloc(sizeof(*result));
  memset(result, 0, sizeof(*result));
  gpr_ref_init(&result->refs, 1);
  result->addresses = addresses;
  result->lb_policy_name = gpr_strdup(lb_policy_name);
  method_config_table_init(&result->method_configs);
  return result;
}

void grpc_resolver_result_ref(grpc_resolver_result* result) {
  gpr_ref(&result->refs);
}

void grpc_resolver_result_unref(grpc_exec_ctx* exec_ctx,
                                grpc_resolver_result* result) {
  if (gpr_unref(&result->refs)) {
    grpc_addresses_destroy(result->addresses);
    gpr_free(result->lb_policy_name);
    method_config_table_destroy(&result->method_configs);
    gpr_free(result);
  }
}

grpc_addresses* grpc_resolver_result_get_addresses(
    grpc_resolver_result* result) {
  return result->addresses;
}

const char* grpc_resolver_result_get_lb_policy_name(
    grpc_resolver_result* result) {
  return result->lb_policy_name;
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
  return method_config_table_get(&result->method_configs, path);
}
