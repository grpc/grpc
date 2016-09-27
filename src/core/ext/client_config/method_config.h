//
// Copyright 2016, Google Inc.
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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_METHOD_CONFIG_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_METHOD_CONFIG_H

#include <stdbool.h>

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/transport/hashtable.h"
#include "src/core/lib/transport/metadata.h"

/// Per-method configuration.
typedef struct grpc_method_config grpc_method_config;

/// Any parameter may be NULL to indicate that the value is unset.
grpc_method_config* grpc_method_config_create(
    bool* wait_for_ready, gpr_timespec* timeout,
    int32_t* max_request_message_bytes, int32_t* max_response_message_bytes);

grpc_method_config* grpc_method_config_ref(grpc_method_config* method_config);
void grpc_method_config_unref(grpc_method_config* method_config);

int grpc_method_config_cmp(grpc_method_config* method_config1,
                           grpc_method_config* method_config2);

/// These methods return NULL if the requested field is unset.
/// The caller does NOT take ownership of the result.
bool* grpc_method_config_get_wait_for_ready(grpc_method_config* method_config);
gpr_timespec* grpc_method_config_get_timeout(grpc_method_config* method_config);
int32_t* grpc_method_config_get_max_request_message_bytes(
    grpc_method_config* method_config);
int32_t* grpc_method_config_get_max_response_message_bytes(
    grpc_method_config* method_config);

/// A table of method configs.
typedef grpc_hash_table grpc_method_config_table;

typedef struct grpc_method_config_table_entry {
  /// The name is of one of the following forms:
  ///   service/method -- specifies exact service and method name
  ///   service/*      -- matches all methods for the specified service
  grpc_mdstr* method_name;
  grpc_method_config* method_config;
} grpc_method_config_table_entry;

/// Takes new references to all keys and values in \a entries.
grpc_method_config_table* grpc_method_config_table_create(
    size_t num_entries, grpc_method_config_table_entry* entries);

grpc_method_config_table* grpc_method_config_table_ref(
    grpc_method_config_table* table);
void grpc_method_config_table_unref(grpc_method_config_table* table);

int grpc_method_config_table_cmp(grpc_method_config_table* table1,
                                 grpc_method_config_table* table2);

/// Returns NULL if the method has no config.
/// Caller owns a reference to result.
grpc_method_config* grpc_method_config_table_get_method_config(
    grpc_method_config_table* table, grpc_mdstr* path);

/// Returns a channel arg containing \a table.
grpc_arg grpc_method_config_table_create_channel_arg(
    grpc_method_config_table* table);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_METHOD_CONFIG_H */
