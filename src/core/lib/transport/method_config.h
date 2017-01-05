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

#ifndef GRPC_CORE_LIB_TRANSPORT_METHOD_CONFIG_H
#define GRPC_CORE_LIB_TRANSPORT_METHOD_CONFIG_H

#include <stdbool.h>

#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/transport/mdstr_hash_table.h"
#include "src/core/lib/transport/metadata.h"

/// Per-method configuration.
typedef struct grpc_method_config grpc_method_config;

/// Creates a grpc_method_config with the specified parameters.
/// Any parameter may be NULL to indicate that the value is unset.
///
/// \a wait_for_ready indicates whether the client should wait until the
/// request deadline for the channel to become ready, even if there is a
/// temporary failure before the deadline while attempting to connect.
///
/// \a timeout indicates the timeout for calls.
///
/// \a max_request_message_bytes and \a max_response_message_bytes
/// indicate the maximum sizes of the request (checked when sending) and
/// response (checked when receiving) messages.
grpc_method_config* grpc_method_config_create(
    bool* wait_for_ready, gpr_timespec* timeout,
    int32_t* max_request_message_bytes, int32_t* max_response_message_bytes);

grpc_method_config* grpc_method_config_ref(grpc_method_config* method_config);
void grpc_method_config_unref(grpc_exec_ctx* exec_ctx,
                              grpc_method_config* method_config);

/// Compares two grpc_method_configs.
/// The sort order is stable but undefined.
int grpc_method_config_cmp(const grpc_method_config* method_config1,
                           const grpc_method_config* method_config2);

/// These methods return NULL if the requested field is unset.
/// The caller does NOT take ownership of the result.
const bool* grpc_method_config_get_wait_for_ready(
    const grpc_method_config* method_config);
const gpr_timespec* grpc_method_config_get_timeout(
    const grpc_method_config* method_config);
const int32_t* grpc_method_config_get_max_request_message_bytes(
    const grpc_method_config* method_config);
const int32_t* grpc_method_config_get_max_response_message_bytes(
    const grpc_method_config* method_config);

/// A table of method configs.
typedef grpc_mdstr_hash_table grpc_method_config_table;

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
void grpc_method_config_table_unref(grpc_exec_ctx* exec_ctx,
                                    grpc_method_config_table* table);

/// Compares two grpc_method_config_tables.
/// The sort order is stable but undefined.
int grpc_method_config_table_cmp(const grpc_method_config_table* table1,
                                 const grpc_method_config_table* table2);

/// Gets the method config for the specified \a path, which should be of
/// the form "/service/method".
/// Returns NULL if the method has no config.
/// Caller does NOT own a reference to the result.
///
/// Note: This returns a void* instead of a grpc_method_config* so that
/// it can also be used for tables constructed via
/// grpc_method_config_table_convert().
void* grpc_method_config_table_get(grpc_exec_ctx* exec_ctx,
                                   const grpc_mdstr_hash_table* table,
                                   const grpc_mdstr* path);

/// Returns a channel arg containing \a table.
grpc_arg grpc_method_config_table_create_channel_arg(
    grpc_method_config_table* table);

/// Generates a new table from \a table whose values are converted to a
/// new form via the \a convert_value function.  The new table will use
/// \a vtable for its values.
///
/// This is generally used to convert the table's value type from
/// grpc_method_config to a simple struct containing only the parameters
/// relevant to a particular filter, thus avoiding the need for a hash
/// table lookup on the fast path.  In that scenario, \a convert_value
/// will return a new instance of the struct containing the values from
/// the grpc_method_config, and \a vtable provides the methods for
/// operating on the struct type.
grpc_mdstr_hash_table* grpc_method_config_table_convert(
    grpc_exec_ctx* exec_ctx, const grpc_method_config_table* table,
    void* (*convert_value)(const grpc_method_config* method_config),
    const grpc_mdstr_hash_table_vtable* vtable);

#endif /* GRPC_CORE_LIB_TRANSPORT_METHOD_CONFIG_H */
