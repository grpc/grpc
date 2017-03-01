//
// Copyright 2016, gRPC authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_CORE_LIB_TRANSPORT_SERVICE_CONFIG_H
#define GRPC_CORE_LIB_TRANSPORT_SERVICE_CONFIG_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_hash_table.h"

typedef struct grpc_service_config grpc_service_config;

grpc_service_config* grpc_service_config_create(const char* json_string);
void grpc_service_config_destroy(grpc_service_config* service_config);

/// Gets the LB policy name from \a service_config.
/// Returns NULL if no LB policy name was specified.
/// Caller does NOT take ownership.
const char* grpc_service_config_get_lb_policy_name(
    const grpc_service_config* service_config);

/// Creates a method config table based on the data in \a json.
/// The table's keys are request paths.  The table's value type is
/// returned by \a create_value(), based on data parsed from the JSON tree.
/// \a vtable provides methods used to manage the values.
/// Returns NULL on error.
grpc_slice_hash_table* grpc_service_config_create_method_config_table(
    grpc_exec_ctx* exec_ctx, const grpc_service_config* service_config,
    void* (*create_value)(const grpc_json* method_config_json),
    const grpc_slice_hash_table_vtable* vtable);

/// A helper function for looking up values in the table returned by
/// \a grpc_service_config_create_method_config_table().
/// Gets the method config for the specified \a path, which should be of
/// the form "/service/method".
/// Returns NULL if the method has no config.
/// Caller does NOT own a reference to the result.
void* grpc_method_config_table_get(grpc_exec_ctx* exec_ctx,
                                   const grpc_slice_hash_table* table,
                                   grpc_slice path);

#endif /* GRPC_CORE_LIB_TRANSPORT_SERVICE_CONFIG_H */
