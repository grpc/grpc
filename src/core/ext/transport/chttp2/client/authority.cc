/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/client/authority.h"

grpc_channel_args* grpc_default_authority_add_if_not_present(
    const grpc_channel_args* args) {
  const bool has_default_authority =
      grpc_channel_args_find(args, GRPC_ARG_DEFAULT_AUTHORITY) != nullptr;
  grpc_arg new_args[1];
  size_t num_new_args = 0;
  std::string default_authority;
  if (!has_default_authority) {
    const grpc_arg* server_uri_arg =
        grpc_channel_args_find(args, GRPC_ARG_SERVER_URI);
    const char* server_uri_str = grpc_channel_arg_get_string(server_uri_arg);
    GPR_ASSERT(server_uri_str != nullptr);
    default_authority =
        grpc_core::ResolverRegistry::GetDefaultAuthority(server_uri_str);
    new_args[num_new_args++] = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
        const_cast<char*>(default_authority.c_str()));
  }
  return grpc_channel_args_copy_and_add(args, new_args, num_new_args);
}
