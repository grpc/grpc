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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_RESOLVER_RESULT_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_RESOLVER_RESULT_H

#include "src/core/ext/client_config/lb_policy_factory.h"
#include "src/core/lib/iomgr/resolve_address.h"

// TODO(roth, ctiller): In the long term, we are considering replacing
// the resolver_result data structure with grpc_channel_args.  The idea is
// that the resolver will return a set of channel args that contains the
// information that is currently in the resolver_result struct.  For
// example, there will be specific args indicating the set of addresses
// and the name of the LB policy to instantiate.  Note that if we did
// this, we would probably want to change the data structure of
// grpc_channel_args such to a hash table or AVL or some other data
// structure that does not require linear search to find keys.

/// Per-method configuration.

typedef struct grpc_method_config grpc_method_config;

/// Any parameter may be NULL to indicate that the value is unset.
grpc_method_config* grpc_method_config_create(
    bool* wait_for_ready, gpr_timespec* timeout,
    int32_t* max_request_message_bytes, int32_t* max_response_message_bytes);

grpc_method_config* grpc_method_config_ref(grpc_method_config* method_config);
void grpc_method_config_unref(grpc_method_config* method_config);

/// These methods return NULL if the requested field is unset.
/// The caller does NOT take ownership of the result.
bool* grpc_method_config_get_wait_for_ready(grpc_method_config* method_config);
gpr_timespec* grpc_method_config_get_timeout(grpc_method_config* method_config);
int32_t* grpc_method_config_get_max_request_message_bytes(
    grpc_method_config* method_config);
int32_t* grpc_method_config_get_max_response_message_bytes(
    grpc_method_config* method_config);

/// Results reported from a grpc_resolver.
typedef struct grpc_resolver_result grpc_resolver_result;

/// Takes ownership of \a addresses and \a lb_policy_args.
grpc_resolver_result* grpc_resolver_result_create(
    const char* server_name, grpc_lb_addresses* addresses,
    const char* lb_policy_name, grpc_channel_args* lb_policy_args);
void grpc_resolver_result_ref(grpc_resolver_result* result);
void grpc_resolver_result_unref(grpc_exec_ctx* exec_ctx,
                                grpc_resolver_result* result);

/// Caller does NOT take ownership of result.
const char* grpc_resolver_result_get_server_name(grpc_resolver_result* result);

/// Caller does NOT take ownership of result.
grpc_lb_addresses* grpc_resolver_result_get_addresses(
    grpc_resolver_result* result);

/// Caller does NOT take ownership of result.
const char* grpc_resolver_result_get_lb_policy_name(
    grpc_resolver_result* result);

/// Caller does NOT take ownership of result.
grpc_channel_args* grpc_resolver_result_get_lb_policy_args(
    grpc_resolver_result* result);

/// Adds a method config.  \a paths indicates the set of path names
/// for which this config applies.  Each name is of one of the following
/// forms:
///   service/method -- specifies exact service and method name
///   service/*      -- matches all methods for the specified service
///   *              -- matches all methods for all services
/// Takes new references to all elements of \a paths and to \a method_config.
void grpc_resolver_result_add_method_config(grpc_resolver_result* result,
                                            grpc_mdstr** paths,
                                            size_t num_paths,
                                            grpc_method_config* method_config);

/// Returns NULL if the method has no config.
/// Caller does NOT take ownership of result.
grpc_method_config* grpc_resolver_result_get_method_config(
    grpc_resolver_result* result, grpc_mdstr* path);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_RESOLVER_RESULT_H */
