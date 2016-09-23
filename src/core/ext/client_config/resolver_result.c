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
// grpc_resolver_result
//

struct grpc_resolver_result {
  gpr_refcount refs;
  char* server_name;
  grpc_lb_addresses* addresses;
  char* lb_policy_name;
  grpc_channel_args* lb_policy_args;
  grpc_method_config_table* method_configs;
};

grpc_resolver_result* grpc_resolver_result_create(
    const char* server_name, grpc_lb_addresses* addresses,
    const char* lb_policy_name, grpc_channel_args* lb_policy_args,
    grpc_method_config_table* method_configs) {
  grpc_resolver_result* result = gpr_malloc(sizeof(*result));
  memset(result, 0, sizeof(*result));
  gpr_ref_init(&result->refs, 1);
  result->server_name = gpr_strdup(server_name);
  result->addresses = addresses;
  result->lb_policy_name = gpr_strdup(lb_policy_name);
  result->lb_policy_args = lb_policy_args;
  result->method_configs = grpc_method_config_table_ref(method_configs);
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
    grpc_method_config_table_unref(result->method_configs);
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

grpc_method_config_table* grpc_resolver_result_get_method_configs(
    grpc_resolver_result* result) {
  return result->method_configs;
}
