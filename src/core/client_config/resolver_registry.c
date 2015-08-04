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

static registered_resolver g_all_of_the_resolvers[MAX_RESOLVERS];
static int g_number_of_resolvers = 0;

static char *g_default_resolver_scheme;

void grpc_resolver_registry_init(const char *default_resolver_scheme) {
  g_number_of_resolvers = 0;
  g_default_resolver_scheme = gpr_strdup(default_resolver_scheme);
}

void grpc_resolver_registry_shutdown(void) {
  int i;
  for (i = 0; i < g_number_of_resolvers; i++) {
    gpr_free(g_all_of_the_resolvers[i].scheme);
    grpc_resolver_factory_unref(g_all_of_the_resolvers[i].factory);
  }
  gpr_free(g_default_resolver_scheme);
}

void grpc_register_resolver_type(const char *scheme,
                                 grpc_resolver_factory *factory) {
  int i;
  for (i = 0; i < g_number_of_resolvers; i++) {
    GPR_ASSERT(0 != strcmp(scheme, g_all_of_the_resolvers[i].scheme));
  }
  GPR_ASSERT(g_number_of_resolvers != MAX_RESOLVERS);
  g_all_of_the_resolvers[g_number_of_resolvers].scheme = gpr_strdup(scheme);
  grpc_resolver_factory_ref(factory);
  g_all_of_the_resolvers[g_number_of_resolvers].factory = factory;
  g_number_of_resolvers++;
}

static grpc_resolver_factory *lookup_factory(grpc_uri *uri) {
  int i;

  /* handling NULL uri's here simplifies grpc_resolver_create */
  if (!uri) return NULL;

  for (i = 0; i < g_number_of_resolvers; i++) {
    if (0 == strcmp(uri->scheme, g_all_of_the_resolvers[i].scheme)) {
      return g_all_of_the_resolvers[i].factory;
    }
  }

  return NULL;
}

grpc_resolver *grpc_resolver_create(
    const char *name, grpc_subchannel_factory *subchannel_factory) {
  grpc_uri *uri;
  char *tmp;
  grpc_resolver_factory *factory = NULL;
  grpc_resolver *resolver;

  uri = grpc_uri_parse(name, 1);
  factory = lookup_factory(uri);
  if (factory == NULL && g_default_resolver_scheme != NULL) {
    grpc_uri_destroy(uri);
    gpr_asprintf(&tmp, "%s%s", g_default_resolver_scheme, name);
    uri = grpc_uri_parse(tmp, 1);
    factory = lookup_factory(uri);
    if (factory == NULL) {
      grpc_uri_destroy(grpc_uri_parse(name, 0));
      grpc_uri_destroy(grpc_uri_parse(tmp, 0));
      gpr_log(GPR_ERROR, "don't know how to resolve '%s' or '%s'", name, tmp);
    }
    gpr_free(tmp);
  } else if (factory == NULL) {
    grpc_uri_destroy(grpc_uri_parse(name, 0));
    gpr_log(GPR_ERROR, "don't know how to resolve '%s'", name);
  }
  resolver =
      grpc_resolver_factory_create_resolver(factory, uri, subchannel_factory);
  grpc_uri_destroy(uri);
  return resolver;
}
