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

#include "src/core/ext/client_channel/resolver_registry.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#define MAX_RESOLVERS 10
#define DEFAULT_RESOLVER_PREFIX_MAX_LENGTH 32

static grpc_resolver_factory *g_all_of_the_resolvers[MAX_RESOLVERS];
static int g_number_of_resolvers = 0;

static char g_default_resolver_prefix[DEFAULT_RESOLVER_PREFIX_MAX_LENGTH] =
    "dns:///";

void grpc_resolver_registry_init() {}

void grpc_resolver_registry_shutdown(void) {
  for (int i = 0; i < g_number_of_resolvers; i++) {
    grpc_resolver_factory_unref(g_all_of_the_resolvers[i]);
  }
  // FIXME(ctiller): this should live in grpc_resolver_registry_init,
  // however that would have the client_channel plugin call this AFTER we start
  // registering resolvers from third party plugins, and so they'd never show
  // up.
  // We likely need some kind of dependency system for plugins.... what form
  // that takes is TBD.
  g_number_of_resolvers = 0;
}

void grpc_resolver_registry_set_default_prefix(
    const char *default_resolver_prefix) {
  const size_t len = strlen(default_resolver_prefix);
  GPR_ASSERT(len < DEFAULT_RESOLVER_PREFIX_MAX_LENGTH &&
             "default resolver prefix too long");
  GPR_ASSERT(len > 0 && "default resolver prefix can't be empty");
  // By the previous assert, default_resolver_prefix is safe to be copied with a
  // plain strcpy.
  strcpy(g_default_resolver_prefix, default_resolver_prefix);
}

void grpc_register_resolver_type(grpc_resolver_factory *factory) {
  int i;
  for (i = 0; i < g_number_of_resolvers; i++) {
    GPR_ASSERT(0 != strcmp(factory->vtable->scheme,
                           g_all_of_the_resolvers[i]->vtable->scheme));
  }
  GPR_ASSERT(g_number_of_resolvers != MAX_RESOLVERS);
  grpc_resolver_factory_ref(factory);
  g_all_of_the_resolvers[g_number_of_resolvers++] = factory;
}

static grpc_resolver_factory *lookup_factory(const char *name) {
  int i;

  for (i = 0; i < g_number_of_resolvers; i++) {
    if (0 == strcmp(name, g_all_of_the_resolvers[i]->vtable->scheme)) {
      return g_all_of_the_resolvers[i];
    }
  }

  return NULL;
}

grpc_resolver_factory *grpc_resolver_factory_lookup(const char *name) {
  grpc_resolver_factory *f = lookup_factory(name);
  if (f) grpc_resolver_factory_ref(f);
  return f;
}

static grpc_resolver_factory *lookup_factory_by_uri(grpc_uri *uri) {
  if (!uri) return NULL;
  return lookup_factory(uri->scheme);
}

static grpc_resolver_factory *resolve_factory(grpc_exec_ctx *exec_ctx,
                                              const char *target,
                                              grpc_uri **uri,
                                              char **canonical_target) {
  grpc_resolver_factory *factory = NULL;

  GPR_ASSERT(uri != NULL);
  *uri = grpc_uri_parse(exec_ctx, target, 1);
  factory = lookup_factory_by_uri(*uri);
  if (factory == NULL) {
    grpc_uri_destroy(*uri);
    gpr_asprintf(canonical_target, "%s%s", g_default_resolver_prefix, target);
    *uri = grpc_uri_parse(exec_ctx, *canonical_target, 1);
    factory = lookup_factory_by_uri(*uri);
    if (factory == NULL) {
      grpc_uri_destroy(grpc_uri_parse(exec_ctx, target, 0));
      grpc_uri_destroy(grpc_uri_parse(exec_ctx, *canonical_target, 0));
      gpr_log(GPR_ERROR, "don't know how to resolve '%s' or '%s'", target,
              *canonical_target);
    }
  }
  return factory;
}

grpc_resolver *grpc_resolver_create(grpc_exec_ctx *exec_ctx, const char *target,
                                    const grpc_channel_args *args,
                                    grpc_pollset_set *pollset_set,
                                    grpc_combiner *combiner) {
  grpc_uri *uri = NULL;
  char *canonical_target = NULL;
  grpc_resolver_factory *factory =
      resolve_factory(exec_ctx, target, &uri, &canonical_target);
  grpc_resolver *resolver;
  grpc_resolver_args resolver_args;
  memset(&resolver_args, 0, sizeof(resolver_args));
  resolver_args.uri = uri;
  resolver_args.args = args;
  resolver_args.pollset_set = pollset_set;
  resolver_args.combiner = combiner;
  resolver =
      grpc_resolver_factory_create_resolver(exec_ctx, factory, &resolver_args);
  grpc_uri_destroy(uri);
  gpr_free(canonical_target);
  return resolver;
}

char *grpc_get_default_authority(grpc_exec_ctx *exec_ctx, const char *target) {
  grpc_uri *uri = NULL;
  char *canonical_target = NULL;
  grpc_resolver_factory *factory =
      resolve_factory(exec_ctx, target, &uri, &canonical_target);
  char *authority = grpc_resolver_factory_get_default_authority(factory, uri);
  grpc_uri_destroy(uri);
  gpr_free(canonical_target);
  return authority;
}

char *grpc_resolver_factory_add_default_prefix_if_needed(
    grpc_exec_ctx *exec_ctx, const char *target) {
  grpc_uri *uri = NULL;
  char *canonical_target = NULL;
  resolve_factory(exec_ctx, target, &uri, &canonical_target);
  grpc_uri_destroy(uri);
  return canonical_target == NULL ? gpr_strdup(target) : canonical_target;
}
