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

/// Results reported from a grpc_resolver.
typedef struct grpc_resolver_result grpc_resolver_result;

/// Takes ownership of \a addresses, \a lb_policy_args.
grpc_resolver_result* grpc_resolver_result_create(
    const char* server_name, grpc_lb_addresses* addresses,
    const char* lb_policy_name, grpc_channel_args* lb_policy_args);

void grpc_resolver_result_ref(grpc_resolver_result* result);
void grpc_resolver_result_unref(grpc_exec_ctx* exec_ctx,
                                grpc_resolver_result* result);

/// Accessors.  Caller does NOT take ownership of results.
const char* grpc_resolver_result_get_server_name(grpc_resolver_result* result);
grpc_lb_addresses* grpc_resolver_result_get_addresses(
    grpc_resolver_result* result);
const char* grpc_resolver_result_get_lb_policy_name(
    grpc_resolver_result* result);
grpc_channel_args* grpc_resolver_result_get_lb_policy_args(
    grpc_resolver_result* result);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_RESOLVER_RESULT_H */
