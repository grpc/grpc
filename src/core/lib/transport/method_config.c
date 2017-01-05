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

#include "src/core/lib/transport/method_config.h"

#include <string.h>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/transport/mdstr_hash_table.h"
#include "src/core/lib/transport/metadata.h"

//
// grpc_method_config
//

// bool vtable

static void* bool_copy(void* valuep) {
  bool value = *(bool*)valuep;
  bool* new_value = gpr_malloc(sizeof(bool));
  *new_value = value;
  return new_value;
}

static int bool_cmp(void* v1, void* v2) {
  bool b1 = *(bool*)v1;
  bool b2 = *(bool*)v2;
  if (!b1 && b2) return -1;
  if (b1 && !b2) return 1;
  return 0;
}

static void free_mem(grpc_exec_ctx* exec_ctx, void* p) { gpr_free(p); }

static grpc_mdstr_hash_table_vtable bool_vtable = {free_mem, bool_copy,
                                                   bool_cmp};

// timespec vtable

static void* timespec_copy(void* valuep) {
  gpr_timespec value = *(gpr_timespec*)valuep;
  gpr_timespec* new_value = gpr_malloc(sizeof(gpr_timespec));
  *new_value = value;
  return new_value;
}

static int timespec_cmp(void* v1, void* v2) {
  return gpr_time_cmp(*(gpr_timespec*)v1, *(gpr_timespec*)v2);
}

static grpc_mdstr_hash_table_vtable timespec_vtable = {free_mem, timespec_copy,
                                                       timespec_cmp};

// int32 vtable

static void* int32_copy(void* valuep) {
  int32_t value = *(int32_t*)valuep;
  int32_t* new_value = gpr_malloc(sizeof(int32_t));
  *new_value = value;
  return new_value;
}

static int int32_cmp(void* v1, void* v2) {
  int32_t i1 = *(int32_t*)v1;
  int32_t i2 = *(int32_t*)v2;
  if (i1 < i2) return -1;
  if (i1 > i2) return 1;
  return 0;
}

static grpc_mdstr_hash_table_vtable int32_vtable = {free_mem, int32_copy,
                                                    int32_cmp};

// Hash table keys.
#define GRPC_METHOD_CONFIG_WAIT_FOR_READY "grpc.wait_for_ready"  // bool
#define GRPC_METHOD_CONFIG_TIMEOUT "grpc.timeout"                // gpr_timespec
#define GRPC_METHOD_CONFIG_MAX_REQUEST_MESSAGE_BYTES \
  "grpc.max_request_message_bytes"  // int32
#define GRPC_METHOD_CONFIG_MAX_RESPONSE_MESSAGE_BYTES \
  "grpc.max_response_message_bytes"  // int32

struct grpc_method_config {
  grpc_mdstr_hash_table* table;
  grpc_mdstr* wait_for_ready_key;
  grpc_mdstr* timeout_key;
  grpc_mdstr* max_request_message_bytes_key;
  grpc_mdstr* max_response_message_bytes_key;
};

grpc_method_config* grpc_method_config_create(
    bool* wait_for_ready, gpr_timespec* timeout,
    int32_t* max_request_message_bytes, int32_t* max_response_message_bytes) {
  grpc_method_config* method_config = gpr_malloc(sizeof(grpc_method_config));
  memset(method_config, 0, sizeof(grpc_method_config));
  method_config->wait_for_ready_key =
      grpc_mdstr_from_string(GRPC_METHOD_CONFIG_WAIT_FOR_READY);
  method_config->timeout_key =
      grpc_mdstr_from_string(GRPC_METHOD_CONFIG_TIMEOUT);
  method_config->max_request_message_bytes_key =
      grpc_mdstr_from_string(GRPC_METHOD_CONFIG_MAX_REQUEST_MESSAGE_BYTES);
  method_config->max_response_message_bytes_key =
      grpc_mdstr_from_string(GRPC_METHOD_CONFIG_MAX_RESPONSE_MESSAGE_BYTES);
  grpc_mdstr_hash_table_entry entries[4];
  size_t num_entries = 0;
  if (wait_for_ready != NULL) {
    entries[num_entries].key = method_config->wait_for_ready_key;
    entries[num_entries].value = wait_for_ready;
    entries[num_entries].vtable = &bool_vtable;
    ++num_entries;
  }
  if (timeout != NULL) {
    entries[num_entries].key = method_config->timeout_key;
    entries[num_entries].value = timeout;
    entries[num_entries].vtable = &timespec_vtable;
    ++num_entries;
  }
  if (max_request_message_bytes != NULL) {
    entries[num_entries].key = method_config->max_request_message_bytes_key;
    entries[num_entries].value = max_request_message_bytes;
    entries[num_entries].vtable = &int32_vtable;
    ++num_entries;
  }
  if (max_response_message_bytes != NULL) {
    entries[num_entries].key = method_config->max_response_message_bytes_key;
    entries[num_entries].value = max_response_message_bytes;
    entries[num_entries].vtable = &int32_vtable;
    ++num_entries;
  }
  method_config->table = grpc_mdstr_hash_table_create(num_entries, entries);
  return method_config;
}

grpc_method_config* grpc_method_config_ref(grpc_method_config* method_config) {
  grpc_mdstr_hash_table_ref(method_config->table);
  return method_config;
}

void grpc_method_config_unref(grpc_exec_ctx* exec_ctx,
                              grpc_method_config* method_config) {
  if (grpc_mdstr_hash_table_unref(exec_ctx, method_config->table)) {
    GRPC_MDSTR_UNREF(exec_ctx, method_config->wait_for_ready_key);
    GRPC_MDSTR_UNREF(exec_ctx, method_config->timeout_key);
    GRPC_MDSTR_UNREF(exec_ctx, method_config->max_request_message_bytes_key);
    GRPC_MDSTR_UNREF(exec_ctx, method_config->max_response_message_bytes_key);
    gpr_free(method_config);
  }
}

int grpc_method_config_cmp(const grpc_method_config* method_config1,
                           const grpc_method_config* method_config2) {
  return grpc_mdstr_hash_table_cmp(method_config1->table,
                                   method_config2->table);
}

const bool* grpc_method_config_get_wait_for_ready(
    const grpc_method_config* method_config) {
  return grpc_mdstr_hash_table_get(method_config->table,
                                   method_config->wait_for_ready_key);
}

const gpr_timespec* grpc_method_config_get_timeout(
    const grpc_method_config* method_config) {
  return grpc_mdstr_hash_table_get(method_config->table,
                                   method_config->timeout_key);
}

const int32_t* grpc_method_config_get_max_request_message_bytes(
    const grpc_method_config* method_config) {
  return grpc_mdstr_hash_table_get(
      method_config->table, method_config->max_request_message_bytes_key);
}

const int32_t* grpc_method_config_get_max_response_message_bytes(
    const grpc_method_config* method_config) {
  return grpc_mdstr_hash_table_get(
      method_config->table, method_config->max_response_message_bytes_key);
}

//
// grpc_method_config_table
//

static void method_config_unref(grpc_exec_ctx* exec_ctx, void* valuep) {
  grpc_method_config_unref(exec_ctx, valuep);
}

static void* method_config_ref(void* valuep) {
  return grpc_method_config_ref(valuep);
}

static int method_config_cmp(void* valuep1, void* valuep2) {
  return grpc_method_config_cmp(valuep1, valuep2);
}

static const grpc_mdstr_hash_table_vtable method_config_table_vtable = {
    method_config_unref, method_config_ref, method_config_cmp};

grpc_method_config_table* grpc_method_config_table_create(
    size_t num_entries, grpc_method_config_table_entry* entries) {
  grpc_mdstr_hash_table_entry* hash_table_entries =
      gpr_malloc(sizeof(grpc_mdstr_hash_table_entry) * num_entries);
  for (size_t i = 0; i < num_entries; ++i) {
    hash_table_entries[i].key = entries[i].method_name;
    hash_table_entries[i].value = entries[i].method_config;
    hash_table_entries[i].vtable = &method_config_table_vtable;
  }
  grpc_method_config_table* method_config_table =
      grpc_mdstr_hash_table_create(num_entries, hash_table_entries);
  gpr_free(hash_table_entries);
  return method_config_table;
}

grpc_method_config_table* grpc_method_config_table_ref(
    grpc_method_config_table* table) {
  return grpc_mdstr_hash_table_ref(table);
}

void grpc_method_config_table_unref(grpc_exec_ctx* exec_ctx,
                                    grpc_method_config_table* table) {
  grpc_mdstr_hash_table_unref(exec_ctx, table);
}

int grpc_method_config_table_cmp(const grpc_method_config_table* table1,
                                 const grpc_method_config_table* table2) {
  return grpc_mdstr_hash_table_cmp(table1, table2);
}

void* grpc_method_config_table_get(grpc_exec_ctx* exec_ctx,
                                   const grpc_mdstr_hash_table* table,
                                   const grpc_mdstr* path) {
  void* value = grpc_mdstr_hash_table_get(table, path);
  // If we didn't find a match for the path, try looking for a wildcard
  // entry (i.e., change "/service/method" to "/service/*").
  if (value == NULL) {
    const char* path_str = grpc_mdstr_as_c_string(path);
    const char* sep = strrchr(path_str, '/') + 1;
    const size_t len = (size_t)(sep - path_str);
    char* buf = gpr_malloc(len + 2);  // '*' and NUL
    memcpy(buf, path_str, len);
    buf[len] = '*';
    buf[len + 1] = '\0';
    grpc_mdstr* wildcard_path = grpc_mdstr_from_string(buf);
    gpr_free(buf);
    value = grpc_mdstr_hash_table_get(table, wildcard_path);
    GRPC_MDSTR_UNREF(exec_ctx, wildcard_path);
  }
  return value;
}

static void* copy_arg(void* p) { return grpc_method_config_table_ref(p); }

static void destroy_arg(grpc_exec_ctx* exec_ctx, void* p) {
  grpc_method_config_table_unref(exec_ctx, p);
}

static int cmp_arg(void* p1, void* p2) {
  return grpc_method_config_table_cmp(p1, p2);
}

static grpc_arg_pointer_vtable arg_vtable = {copy_arg, destroy_arg, cmp_arg};

grpc_arg grpc_method_config_table_create_channel_arg(
    grpc_method_config_table* table) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_SERVICE_CONFIG;
  arg.value.pointer.p = table;
  arg.value.pointer.vtable = &arg_vtable;
  return arg;
}

// State used by convert_entry() below.
typedef struct conversion_state {
  void* (*convert_value)(const grpc_method_config* method_config);
  const grpc_mdstr_hash_table_vtable* vtable;
  size_t num_entries;
  grpc_mdstr_hash_table_entry* entries;
} conversion_state;

// A function to be passed to grpc_mdstr_hash_table_iterate() to create
// a copy of the entries.
static void convert_entry(const grpc_mdstr_hash_table_entry* entry,
                          void* user_data) {
  conversion_state* state = user_data;
  state->entries[state->num_entries].key = GRPC_MDSTR_REF(entry->key);
  state->entries[state->num_entries].value = state->convert_value(entry->value);
  state->entries[state->num_entries].vtable = state->vtable;
  ++state->num_entries;
}

grpc_mdstr_hash_table* grpc_method_config_table_convert(
    grpc_exec_ctx* exec_ctx, const grpc_method_config_table* table,
    void* (*convert_value)(const grpc_method_config* method_config),
    const grpc_mdstr_hash_table_vtable* vtable) {
  // Create an array of the entries in the table with converted values.
  conversion_state state;
  state.convert_value = convert_value;
  state.vtable = vtable;
  state.num_entries = 0;
  state.entries = gpr_malloc(sizeof(grpc_mdstr_hash_table_entry) *
                             grpc_mdstr_hash_table_num_entries(table));
  grpc_mdstr_hash_table_iterate(table, convert_entry, &state);
  // Create a new table based on the array we just constructed.
  grpc_mdstr_hash_table* new_table =
      grpc_mdstr_hash_table_create(state.num_entries, state.entries);
  // Clean up the array.
  for (size_t i = 0; i < state.num_entries; ++i) {
    GRPC_MDSTR_UNREF(exec_ctx, state.entries[i].key);
    vtable->destroy_value(exec_ctx, state.entries[i].value);
  }
  gpr_free(state.entries);
  // Return the new table.
  return new_table;
}
