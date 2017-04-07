//
// Copyright 2016, Google Inc.
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

// This is similar to the sockaddr resolver, except that it supports a
// bunch of query args that are useful for dependency injection in tests.

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"

#include "test/core/end2end/fake_resolver.h"

#define GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR \
  "grpc.fake_resolver.response_generator"

//
// fake_resolver
//

typedef struct {
  // base class -- must be first
  grpc_resolver base;

  // passed-in parameters
  grpc_channel_args* results;

  // have we published?
  bool published;
  // pending next completion, or NULL
  grpc_closure* next_completion;
  // target result address for next completion
  grpc_channel_args** target_result;
} fake_resolver;

static void fake_resolver_destroy(grpc_exec_ctx* exec_ctx, grpc_resolver* gr) {
  fake_resolver* r = (fake_resolver*)gr;
  grpc_channel_args_destroy(exec_ctx, r->results);
  gpr_free(r);
}

static void fake_resolver_shutdown_locked(grpc_exec_ctx* exec_ctx,
                                          grpc_resolver* resolver) {
  fake_resolver* r = (fake_resolver*)resolver;
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    grpc_closure_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
  }
}

static void fake_resolver_maybe_finish_next_locked(grpc_exec_ctx* exec_ctx,
                                                   fake_resolver* r) {
  if (r->next_completion != NULL && !r->published) {
    r->published = true;
    *r->target_result = grpc_channel_args_copy(r->results);
    grpc_closure_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
  }
}

static void fake_resolver_channel_saw_error_locked(grpc_exec_ctx* exec_ctx,
                                                   grpc_resolver* resolver) {
  fake_resolver* r = (fake_resolver*)resolver;
  r->published = false;
  fake_resolver_maybe_finish_next_locked(exec_ctx, r);
}

static void fake_resolver_next_locked(grpc_exec_ctx* exec_ctx,
                                      grpc_resolver* resolver,
                                      grpc_channel_args** target_result,
                                      grpc_closure* on_complete) {
  fake_resolver* r = (fake_resolver*)resolver;
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_result = target_result;
  fake_resolver_maybe_finish_next_locked(exec_ctx, r);
}

static const grpc_resolver_vtable fake_resolver_vtable = {
    fake_resolver_destroy, fake_resolver_shutdown_locked,
    fake_resolver_channel_saw_error_locked, fake_resolver_next_locked};

struct grpc_fake_resolver_response_generator {
  fake_resolver* resolver;  // Set by the fake_resolver constructor to itself.
  gpr_refcount refcount;
};

grpc_fake_resolver_response_generator*
grpc_fake_resolver_response_generator_create() {
  grpc_fake_resolver_response_generator* generator =
      gpr_zalloc(sizeof(*generator));
  gpr_ref_init(&generator->refcount, 1);
  return generator;
}

grpc_fake_resolver_response_generator*
grpc_fake_resolver_response_generator_ref(
    grpc_fake_resolver_response_generator* generator) {
  gpr_ref(&generator->refcount);
  return generator;
}

void grpc_fake_resolver_response_generator_unref(
    grpc_fake_resolver_response_generator* generator) {
  if (gpr_unref(&generator->refcount)) {
    gpr_free(generator);
  }
}

void grpc_fake_resolver_response_generator_set_response_locked(
    grpc_exec_ctx* exec_ctx,
    const grpc_fake_resolver_response_generator* response_generator,
    const grpc_channel_args* results) {
  GPR_ASSERT(response_generator->resolver != NULL);
  fake_resolver* r = response_generator->resolver;
  if (r->results != NULL) {
    grpc_channel_args_destroy(exec_ctx, r->results);
  }
  r->results = grpc_channel_args_copy(results);
  r->published = false;
  fake_resolver_maybe_finish_next_locked(exec_ctx, r);
}

static void* response_generator_arg_copy(void* p) {
  return grpc_fake_resolver_response_generator_ref(p);
}

static void response_generator_arg_destroy(grpc_exec_ctx* exec_ctx, void* p) {
  grpc_fake_resolver_response_generator_unref(p);
}

static int response_generator_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

static const grpc_arg_pointer_vtable response_generator_arg_vtable = {
    response_generator_arg_copy, response_generator_arg_destroy,
    response_generator_cmp};

grpc_arg grpc_fake_resolver_response_generator_arg(
    grpc_fake_resolver_response_generator* generator) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR;
  arg.value.pointer.p = generator;
  arg.value.pointer.vtable = &response_generator_arg_vtable;
  return arg;
}

grpc_fake_resolver_response_generator*
grpc_fake_resolver_get_response_generator(const grpc_channel_args* args) {
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR);
  if (arg == NULL || arg->type != GRPC_ARG_POINTER) return NULL;
  return arg->value.pointer.p;
}

grpc_channel_args* grpc_fake_resolver_response_create(
    const char** uris, const char** balancer_names, const bool* is_balancer,
    size_t num_items) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_lb_addresses* addresses = grpc_lb_addresses_create(num_items, NULL);
  bool errors_found = false;
  size_t i;
  for (i = 0; i < num_items; ++i) {
    grpc_uri* ith_uri = grpc_uri_parse(&exec_ctx, uris[i], true);
    if (!grpc_parse_ipv4(ith_uri, &addresses->addresses[i].address)) {
      errors_found = true;
    }
    grpc_uri_destroy(ith_uri);
    if (errors_found) goto done_with_error;
    addresses->addresses[i].balancer_name = gpr_strdup(balancer_names[i]);
    addresses->addresses[i].is_balancer = is_balancer[i];
  }
  const grpc_arg addresses_arg =
      grpc_lb_addresses_create_channel_arg(addresses);
  grpc_channel_args* response =
      grpc_channel_args_copy_and_add(NULL, &addresses_arg, 1);
  grpc_lb_addresses_destroy(&exec_ctx, addresses);
  return response;
done_with_error:
  for (size_t j = 0; j < i; ++j) {
    gpr_free(addresses->addresses[i].balancer_name);
  }
  gpr_free(addresses->addresses);
  gpr_free(addresses);
  return NULL;
}

//
// fake_resolver_factory
//

static void fake_resolver_factory_ref(grpc_resolver_factory* factory) {}

static void fake_resolver_factory_unref(grpc_resolver_factory* factory) {}

static grpc_resolver* fake_resolver_create(grpc_exec_ctx* exec_ctx,
                                           grpc_resolver_factory* factory,
                                           grpc_resolver_args* args) {
  fake_resolver* r = gpr_zalloc(sizeof(*r));
  grpc_resolver_init(&r->base, &fake_resolver_vtable, args->combiner);
  grpc_fake_resolver_response_generator* response_generator =
      grpc_fake_resolver_get_response_generator(args->args);
  if (response_generator != NULL) response_generator->resolver = r;
  return &r->base;
}

static char* fake_resolver_get_default_authority(grpc_resolver_factory* factory,
                                                 grpc_uri* uri) {
  const char* path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static const grpc_resolver_factory_vtable fake_resolver_factory_vtable = {
    fake_resolver_factory_ref, fake_resolver_factory_unref,
    fake_resolver_create, fake_resolver_get_default_authority, "test"};

static grpc_resolver_factory fake_resolver_factory = {
    &fake_resolver_factory_vtable};

void grpc_fake_resolver_init(void) {
  grpc_register_resolver_type(&fake_resolver_factory);
}
