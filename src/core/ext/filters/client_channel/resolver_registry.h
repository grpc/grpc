/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_REGISTRY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_REGISTRY_H

#include "src/core/ext/filters/client_channel/resolver_factory.h"
#include "src/core/lib/iomgr/pollset_set.h"

#ifdef __cplusplus
extern "C" {
#endif

void grpc_resolver_registry_init();
void grpc_resolver_registry_shutdown(void);

/** Set the default URI prefix to \a default_prefix. */
void grpc_resolver_registry_set_default_prefix(const char* default_prefix);

/** Register a resolver type.
    URI's of \a scheme will be resolved with the given resolver.
    If \a priority is greater than zero, then the resolver will be eligible
    to resolve names that are passed in with no scheme. Higher priority
    resolvers will be tried before lower priority schemes. */
void grpc_register_resolver_type(grpc_resolver_factory* factory);

/** Create a resolver given \a target.
    First tries to parse \a target as a URI. If this succeeds, tries
    to locate a registered resolver factory based on the URI scheme.
    If parsing or location fails, prefixes default_prefix from
    grpc_resolver_registry_init to target, and tries again (if default_prefix
    was not NULL).
    If a resolver factory was found, use it to instantiate a resolver and
    return it.
    If a resolver factory was not found, return NULL.
    \a args is a set of channel arguments to be included in the result
    (typically the set of arguments passed in from the client API).
    \a pollset_set is used to drive IO in the name resolution process, it
    should not be NULL. */
grpc_resolver* grpc_resolver_create(grpc_exec_ctx* exec_ctx, const char* target,
                                    const grpc_channel_args* args,
                                    grpc_pollset_set* pollset_set,
                                    grpc_combiner* combiner);

/** Find a resolver factory given a name and return an (owned-by-the-caller)
 *  reference to it */
grpc_resolver_factory* grpc_resolver_factory_lookup(const char* name);

/** Given a target, return a (freshly allocated with gpr_malloc) string
    representing the default authority to pass from a client. */
char* grpc_get_default_authority(grpc_exec_ctx* exec_ctx, const char* target);

/** Returns a newly allocated string containing \a target, adding the
    default prefix if needed. */
char* grpc_resolver_factory_add_default_prefix_if_needed(
    grpc_exec_ctx* exec_ctx, const char* target);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_REGISTRY_H */
