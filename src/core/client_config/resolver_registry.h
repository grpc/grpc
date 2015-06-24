/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_INTERNAL_CORE_CLIENT_CONFIG_RESOLVER_REGISTRY_H
#define GRPC_INTERNAL_CORE_CLIENT_CONFIG_RESOLVER_REGISTRY_H

#include "src/core/client_config/resolver_factory.h"

void grpc_resolver_registry_init(grpc_resolver_factory *default_resolver);
void grpc_resolver_registry_shutdown(void);

/** Register a resolver type.
    URI's of \a scheme will be resolved with the given resolver.
    If \a priority is greater than zero, then the resolver will be eligible
    to resolve names that are passed in with no scheme. Higher priority
    resolvers will be tried before lower priority schemes. */
void grpc_register_resolver_type(const char *scheme,
                                 grpc_resolver_factory *factory);

/** Create a resolver given a \a uri string (with an optional scheme prefix) */
grpc_resolver *grpc_resolver_create(
    const char *name, grpc_subchannel_factory *subchannel_factory);

#endif /* GRPC_INTERNAL_CORE_CLIENT_CONFIG_RESOLVER_REGISTRY_H */
