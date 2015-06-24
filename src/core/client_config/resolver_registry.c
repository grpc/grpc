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

#include "src/core/client_config/resolver_registry.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#define MAX_RESOLVERS 10

typedef struct {
  char *scheme;
  grpc_resolver_factory *factory;
} registered_resolver;

static registered_resolver all_of_the_resolvers[MAX_RESOLVERS];
static int number_of_resolvers = 0;

void grpc_resolver_registry_init(grpc_resolver_factory *r) {
  number_of_resolvers = 0;
  grpc_register_resolver_type("default-grpc-resolver", r);
}

void grpc_resolver_registry_shutdown(void) {
  int i;
  for (i = 0; i < number_of_resolvers; i++) {
    gpr_free(all_of_the_resolvers[i].scheme);
    grpc_resolver_factory_unref(all_of_the_resolvers[i].factory);
  }
}

void grpc_register_resolver_type(const char *scheme,
                                 grpc_resolver_factory *factory) {
  int i;
  for (i = 0; i < number_of_resolvers; i++) {
    GPR_ASSERT(0 != strcmp(scheme, all_of_the_resolvers[i].scheme));
  }
  GPR_ASSERT(number_of_resolvers != MAX_RESOLVERS);
  all_of_the_resolvers[number_of_resolvers].scheme = gpr_strdup(scheme);
  grpc_resolver_factory_ref(factory);
  all_of_the_resolvers[number_of_resolvers].factory = factory;
  number_of_resolvers++;
}

grpc_resolver *grpc_resolver_create(
    const char *name, grpc_subchannel_factory *subchannel_factory) {
  grpc_uri *uri;
  int i;
  char *tmp;
  grpc_resolver *resolver = NULL;
  if (grpc_has_scheme(name)) {
    uri = grpc_uri_parse(name);
    if (!uri) {
      return NULL;
    }
    for (i = 0; i < number_of_resolvers; i++) {
      if (0 == strcmp(all_of_the_resolvers[i].scheme, uri->scheme)) {
        grpc_resolver_factory_create_resolver(all_of_the_resolvers[i].factory,
                                              uri, subchannel_factory);
      }
    }
  } else {
    gpr_asprintf(&tmp, "default-grpc-resolver:%s", name);
    GPR_ASSERT(grpc_has_scheme(tmp));
    resolver = grpc_resolver_create(tmp, subchannel_factory);
    gpr_free(tmp);
  }
  return resolver;
}
