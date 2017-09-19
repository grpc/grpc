//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"

//
// fake_resolver
//

typedef struct {
  // base class -- must be first
  grpc_resolver base;

  // passed-in parameters
  grpc_channel_args* channel_args;

  // If not NULL, the next set of resolution results to be returned to
  // grpc_resolver_next_locked()'s closure.
  grpc_channel_args* next_results;

  // Results to use for the pretended re-resolution in
  // fake_resolver_channel_saw_error_locked().
  grpc_channel_args* results_upon_error;

  // pending next completion, or NULL
  grpc_closure* next_completion;
  // target result address for next completion
  grpc_channel_args** target_result;
} fake_resolver;

static void fake_resolver_destroy(grpc_exec_ctx* exec_ctx, grpc_resolver* gr) {
  fake_resolver* r = (fake_resolver*)gr;
  grpc_channel_args_destroy(exec_ctx, r->next_results);
  grpc_channel_args_destroy(exec_ctx, r->results_upon_error);
  grpc_channel_args_destroy(exec_ctx, r->channel_args);
  gpr_free(r);
}

static void fake_resolver_shutdown_locked(grpc_exec_ctx* exec_ctx,
                                          grpc_resolver* resolver) {
  fake_resolver* r = (fake_resolver*)resolver;
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    GRPC_CLOSURE_SCHED(
        exec_ctx, r->next_completion,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Resolver Shutdown"));
    r->next_completion = NULL;
  }
}

static void fake_resolver_maybe_finish_next_locked(grpc_exec_ctx* exec_ctx,
                                                   fake_resolver* r) {
  if (r->next_completion != NULL && r->next_results != NULL) {
    *r->target_result =
        grpc_channel_args_union(r->next_results, r->channel_args);
    grpc_channel_args_destroy(exec_ctx, r->next_results);
    r->next_results = NULL;
    GRPC_CLOSURE_SCHED(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
  }
}

static void fake_resolver_channel_saw_error_locked(grpc_exec_ctx* exec_ctx,
                                                   grpc_resolver* resolver) {
  fake_resolver* r = (fake_resolver*)resolver;
  if (r->next_results == NULL && r->results_upon_error != NULL) {
    // Pretend we re-resolved.
    r->next_results = grpc_channel_args_copy(r->results_upon_error);
  }
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
      (grpc_fake_resolver_response_generator*)gpr_zalloc(sizeof(*generator));
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

typedef struct set_response_closure_arg {
  grpc_closure set_response_closure;
  grpc_fake_resolver_response_generator* generator;
  grpc_channel_args* next_response;
} set_response_closure_arg;

static void set_response_closure_fn(grpc_exec_ctx* exec_ctx, void* arg,
                                    grpc_error* error) {
  set_response_closure_arg* closure_arg = (set_response_closure_arg*)arg;
  grpc_fake_resolver_response_generator* generator = closure_arg->generator;
  fake_resolver* r = generator->resolver;
  if (r->next_results != NULL) {
    grpc_channel_args_destroy(exec_ctx, r->next_results);
  }
  r->next_results = closure_arg->next_response;
  if (r->results_upon_error != NULL) {
    grpc_channel_args_destroy(exec_ctx, r->results_upon_error);
  }
  r->results_upon_error = grpc_channel_args_copy(closure_arg->next_response);
  gpr_free(closure_arg);
  fake_resolver_maybe_finish_next_locked(exec_ctx, r);
}

void grpc_fake_resolver_response_generator_set_response(
    grpc_exec_ctx* exec_ctx, grpc_fake_resolver_response_generator* generator,
    grpc_channel_args* next_response) {
  GPR_ASSERT(generator->resolver != NULL);
  set_response_closure_arg* closure_arg =
      (set_response_closure_arg*)gpr_zalloc(sizeof(*closure_arg));
  closure_arg->generator = generator;
  closure_arg->next_response = grpc_channel_args_copy(next_response);
  GRPC_CLOSURE_SCHED(exec_ctx,
                     GRPC_CLOSURE_INIT(&closure_arg->set_response_closure,
                                       set_response_closure_fn, closure_arg,
                                       grpc_combiner_scheduler(
                                           generator->resolver->base.combiner)),
                     GRPC_ERROR_NONE);
}

static void* response_generator_arg_copy(void* p) {
  return grpc_fake_resolver_response_generator_ref(
      (grpc_fake_resolver_response_generator*)p);
}

static void response_generator_arg_destroy(grpc_exec_ctx* exec_ctx, void* p) {
  grpc_fake_resolver_response_generator_unref(
      (grpc_fake_resolver_response_generator*)p);
}

static int response_generator_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

static const grpc_arg_pointer_vtable response_generator_arg_vtable = {
    response_generator_arg_copy, response_generator_arg_destroy,
    response_generator_cmp};

grpc_arg grpc_fake_resolver_response_generator_arg(
    grpc_fake_resolver_response_generator* generator) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = (char*)GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR;
  arg.value.pointer.p = generator;
  arg.value.pointer.vtable = &response_generator_arg_vtable;
  return arg;
}

grpc_fake_resolver_response_generator*
grpc_fake_resolver_get_response_generator(const grpc_channel_args* args) {
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR);
  if (arg == NULL || arg->type != GRPC_ARG_POINTER) return NULL;
  return (grpc_fake_resolver_response_generator*)arg->value.pointer.p;
}

//
// fake_resolver_factory
//

static void fake_resolver_factory_ref(grpc_resolver_factory* factory) {}

static void fake_resolver_factory_unref(grpc_resolver_factory* factory) {}

static grpc_resolver* fake_resolver_create(grpc_exec_ctx* exec_ctx,
                                           grpc_resolver_factory* factory,
                                           grpc_resolver_args* args) {
  fake_resolver* r = (fake_resolver*)gpr_zalloc(sizeof(*r));
  r->channel_args = grpc_channel_args_copy(args->args);
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
    fake_resolver_create, fake_resolver_get_default_authority, "fake"};

static grpc_resolver_factory fake_resolver_factory = {
    &fake_resolver_factory_vtable};

void grpc_resolver_fake_init(void) {
  grpc_register_resolver_type(&fake_resolver_factory);
}

void grpc_resolver_fake_shutdown(void) {}
