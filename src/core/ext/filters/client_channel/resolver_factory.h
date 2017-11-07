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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_FACTORY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_FACTORY_H

#include "src/core/ext/filters/client_channel/client_channel_factory.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/lib/iomgr/pollset_set.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_resolver_factory grpc_resolver_factory;
typedef struct grpc_resolver_factory_vtable grpc_resolver_factory_vtable;

struct grpc_resolver_factory {
  const grpc_resolver_factory_vtable* vtable;
};

typedef struct grpc_resolver_args {
  grpc_uri* uri;
  const grpc_channel_args* args;
  grpc_pollset_set* pollset_set;
  grpc_combiner* combiner;
} grpc_resolver_args;

struct grpc_resolver_factory_vtable {
  void (*ref)(grpc_resolver_factory* factory);
  void (*unref)(grpc_resolver_factory* factory);

  /** Implementation of grpc_resolver_factory_create_resolver */
  grpc_resolver* (*create_resolver)(grpc_exec_ctx* exec_ctx,
                                    grpc_resolver_factory* factory,
                                    grpc_resolver_args* args);

  /** Implementation of grpc_resolver_factory_get_default_authority */
  char* (*get_default_authority)(grpc_resolver_factory* factory, grpc_uri* uri);

  /** URI scheme that this factory implements */
  const char* scheme;
};

void grpc_resolver_factory_ref(grpc_resolver_factory* resolver);
void grpc_resolver_factory_unref(grpc_resolver_factory* resolver);

/** Create a resolver instance for a name */
grpc_resolver* grpc_resolver_factory_create_resolver(
    grpc_exec_ctx* exec_ctx, grpc_resolver_factory* factory,
    grpc_resolver_args* args);

/** Return a (freshly allocated with gpr_malloc) string representing
    the default authority to use for this scheme. */
char* grpc_resolver_factory_get_default_authority(
    grpc_resolver_factory* factory, grpc_uri* uri);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_FACTORY_H */
