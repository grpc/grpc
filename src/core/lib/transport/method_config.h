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

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/transport/mdstr_hash_table.h"

/// Creates a method config table based on the data in \a json.
/// The table's keys are request paths.  The table's value type is
/// returned by \a create_value(), based on data parsed from the JSON tree.
/// \a vtable provides methods used to manage the values.
/// Returns NULL on error.
grpc_mdstr_hash_table* grpc_method_config_table_create_from_json(
    const grpc_json* json,
    void* (*create_value)(const grpc_json* method_config_json),
    const grpc_mdstr_hash_table_vtable* vtable);

/// Gets the method config for the specified \a path, which should be of
/// the form "/service/method".
/// Returns NULL if the method has no config.
/// Caller does NOT own a reference to the result.
void* grpc_method_config_table_get(const grpc_mdstr_hash_table* table,
                                   const grpc_mdstr* path);

/// Creates a channel arg containing \a service_config.
grpc_arg grpc_service_config_create_channel_arg(grpc_json_tree* service_config);

#endif /* GRPC_CORE_LIB_TRANSPORT_METHOD_CONFIG_H */
