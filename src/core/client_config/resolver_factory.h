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

#ifndef GRPC_INTERNAL_CORE_CLIENT_CONFIG_RESOLVER_FACTORY_H
#define GRPC_INTERNAL_CORE_CLIENT_CONFIG_RESOLVER_FACTORY_H

#include "src/core/client_config/resolver.h"
#include "src/core/client_config/subchannel_factory.h"
#include "src/core/client_config/uri_parser.h"

typedef struct grpc_resolver_factory grpc_resolver_factory;
typedef struct grpc_resolver_factory_vtable grpc_resolver_factory_vtable;

/** grpc_resolver provides grpc_client_config objects to grpc_channel
    objects */
struct grpc_resolver_factory {
  const grpc_resolver_factory_vtable *vtable;
};

typedef struct grpc_resolver_args {
  grpc_uri *uri;
  grpc_subchannel_factory *subchannel_factory;
} grpc_resolver_args;

struct grpc_resolver_factory_vtable {
  void (*ref)(grpc_resolver_factory *factory);
  void (*unref)(grpc_resolver_factory *factory);

  /** Implementation of grpc_resolver_factory_create_resolver */
  grpc_resolver *(*create_resolver)(grpc_resolver_factory *factory,
                                    grpc_resolver_args *args);

  /** Implementation of grpc_resolver_factory_get_default_authority */
  char *(*get_default_authority)(grpc_resolver_factory *factory, grpc_uri *uri);

  /** URI scheme that this factory implements */
  const char *scheme;
};

void grpc_resolver_factory_ref(grpc_resolver_factory *resolver);
void grpc_resolver_factory_unref(grpc_resolver_factory *resolver);

/** Create a resolver instance for a name */
grpc_resolver *grpc_resolver_factory_create_resolver(
    grpc_resolver_factory *factory, grpc_resolver_args *args);

/** Return a (freshly allocated with gpr_malloc) string representing
    the default authority to use for this scheme. */
char *grpc_resolver_factory_get_default_authority(
    grpc_resolver_factory *factory, grpc_uri *uri);

#endif /* GRPC_INTERNAL_CORE_CONFIG_RESOLVER_FACTORY_H */
