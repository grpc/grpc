/*
 *
 * Copyright 2020 gRPC authors.
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
#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVAL_ARGS_UTIL_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVAL_ARGS_UTIL_H

#include <grpcpp/impl/codegen/string_ref.h>
#include <map>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/transport/metadata_batch.h"

grpc::string_ref get_path(grpc_metadata_batch* metadata);

grpc::string_ref get_host(grpc_metadata_batch* metadata);

grpc::string_ref get_method(grpc_metadata_batch* metadata);

std::multimap<grpc::string_ref, grpc::string_ref> get_headers(
    grpc_metadata_batch* metadata);

grpc::string_ref get_uri(grpc_auth_context* auth_context);

grpc::string_ref get_server_name(grpc_auth_context* auth_context);

// TODO: source.principal

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVAL_ARGS_UTIL_H