//
// Copyright 2016, gRPC authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// This is similar to the sockaddr resolver, except that it supports a
// bunch of query args that are useful for dependency injection in tests.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_channel/lb_policy_factory.h"
#include "src/core/ext/client_channel/parse_address.h"
#include "src/core/ext/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"

//
// fake_resolver
//

typedef struct {
  // base class -- must be first
  grpc_resolver base;

  // passed-in parameters
  grpc_channel_args* channel_args;
  grpc_lb_addresses* addresses;

  // mutex guarding the rest of the state
  gpr_mu mu;
  // have we published?
  bool published;
  // pending next completion, or NULL
  grpc_closure* next_completion;
  // target result address for next completion
  grpc_channel_args** target_result;
} fake_resolver;

static void fake_resolver_destroy(grpc_exec_ctx* exec_ctx, grpc_resolver* gr) {
  fake_resolver* r = (fake_resolver*)gr;
  gpr_mu_destroy(&r->mu);
  grpc_channel_args_destroy(exec_ctx, r->channel_args);
  grpc_lb_addresses_destroy(exec_ctx, r->addresses);
  gpr_free(r);
}

static void fake_resolver_shutdown(grpc_exec_ctx* exec_ctx,
                                   grpc_resolver* resolver) {
  fake_resolver* r = (fake_resolver*)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    grpc_closure_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void fake_resolver_maybe_finish_next_locked(grpc_exec_ctx* exec_ctx,
                                                   fake_resolver* r) {
  if (r->next_completion != NULL && !r->published) {
    r->published = true;
    grpc_arg arg = grpc_lb_addresses_create_channel_arg(r->addresses);
    *r->target_result =
        grpc_channel_args_copy_and_add(r->channel_args, &arg, 1);
    grpc_closure_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
  }
}

static void fake_resolver_channel_saw_error(grpc_exec_ctx* exec_ctx,
                                            grpc_resolver* resolver) {
  fake_resolver* r = (fake_resolver*)resolver;
  gpr_mu_lock(&r->mu);
  r->published = false;
  fake_resolver_maybe_finish_next_locked(exec_ctx, r);
  gpr_mu_unlock(&r->mu);
}

static void fake_resolver_next(grpc_exec_ctx* exec_ctx, grpc_resolver* resolver,
                               grpc_channel_args** target_result,
                               grpc_closure* on_complete) {
  fake_resolver* r = (fake_resolver*)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_result = target_result;
  fake_resolver_maybe_finish_next_locked(exec_ctx, r);
  gpr_mu_unlock(&r->mu);
}

static const grpc_resolver_vtable fake_resolver_vtable = {
    fake_resolver_destroy, fake_resolver_shutdown,
    fake_resolver_channel_saw_error, fake_resolver_next};

//
// fake_resolver_factory
//

static void fake_resolver_factory_ref(grpc_resolver_factory* factory) {}

static void fake_resolver_factory_unref(grpc_resolver_factory* factory) {}

static void do_nothing(void* ignored) {}

static grpc_resolver* fake_resolver_create(grpc_exec_ctx* exec_ctx,
                                           grpc_resolver_factory* factory,
                                           grpc_resolver_args* args) {
  if (0 != strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based uri's not supported by the %s scheme",
            args->uri->scheme);
    return NULL;
  }
  // Get lb_enabled arg.  Anything other than "0" is interpreted as true.
  const char* lb_enabled_qpart =
      grpc_uri_get_query_arg(args->uri, "lb_enabled");
  const bool lb_enabled =
      lb_enabled_qpart != NULL && strcmp("0", lb_enabled_qpart) != 0;

  // Get the balancer's names.
  const char* balancer_names =
      grpc_uri_get_query_arg(args->uri, "balancer_names");
  grpc_slice_buffer balancer_names_parts;
  grpc_slice_buffer_init(&balancer_names_parts);
  if (balancer_names != NULL) {
    const grpc_slice balancer_names_slice =
        grpc_slice_from_copied_string(balancer_names);
    grpc_slice_split(balancer_names_slice, ",", &balancer_names_parts);
    grpc_slice_unref(balancer_names_slice);
  }

  // Construct addresses.
  grpc_slice path_slice =
      grpc_slice_new(args->uri->path, strlen(args->uri->path), do_nothing);
  grpc_slice_buffer path_parts;
  grpc_slice_buffer_init(&path_parts);
  grpc_slice_split(path_slice, ",", &path_parts);
  if (balancer_names_parts.count > 0 &&
      path_parts.count != balancer_names_parts.count) {
    gpr_log(GPR_ERROR,
            "Balancer names present but mismatched with number of addresses: "
            "%lu balancer names != %lu addresses",
            (unsigned long)balancer_names_parts.count,
            (unsigned long)path_parts.count);
    return NULL;
  }
  grpc_lb_addresses* addresses =
      grpc_lb_addresses_create(path_parts.count, NULL /* user_data_vtable */);
  bool errors_found = false;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    grpc_uri ith_uri = *args->uri;
    char* part_str = grpc_slice_to_c_string(path_parts.slices[i]);
    ith_uri.path = part_str;
    if (!parse_ipv4(&ith_uri, &addresses->addresses[i].address)) {
      errors_found = true;
    }
    gpr_free(part_str);
    if (errors_found) break;
    addresses->addresses[i].is_balancer = lb_enabled;
    addresses->addresses[i].balancer_name =
        balancer_names_parts.count > 0
            ? grpc_dump_slice(balancer_names_parts.slices[i], GPR_DUMP_ASCII)
            : NULL;
  }
  grpc_slice_buffer_destroy_internal(exec_ctx, &path_parts);
  grpc_slice_buffer_destroy_internal(exec_ctx, &balancer_names_parts);
  grpc_slice_unref(path_slice);
  if (errors_found) {
    grpc_lb_addresses_destroy(exec_ctx, addresses);
    return NULL;
  }
  // Instantiate resolver.
  fake_resolver* r = gpr_malloc(sizeof(fake_resolver));
  memset(r, 0, sizeof(*r));
  r->channel_args = grpc_channel_args_copy(args->args);
  r->addresses = addresses;
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &fake_resolver_vtable, args->combiner);
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
