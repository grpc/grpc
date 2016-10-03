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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_config/parse_address.h"
#include "src/core/ext/client_config/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/string.h"

//
// fake_resolver
//

typedef struct {
  // base class -- must be first
  grpc_resolver base;

  // passed-in parameters
  char* target_name;  // the path component of the uri passed in
  grpc_lb_addresses* addresses;
  char* lb_policy_name;

  // mutex guarding the rest of the state
  gpr_mu mu;
  // have we published?
  bool published;
  // pending next completion, or NULL
  grpc_closure* next_completion;
  // target result address for next completion
  grpc_resolver_result** target_result;
} fake_resolver;

static void fake_resolver_destroy(grpc_exec_ctx* exec_ctx, grpc_resolver* gr) {
  fake_resolver* r = (fake_resolver*)gr;
  gpr_mu_destroy(&r->mu);
  gpr_free(r->target_name);
  grpc_lb_addresses_destroy(r->addresses, NULL /* user_data_destroy */);
  gpr_free(r->lb_policy_name);
  gpr_free(r);
}

static void fake_resolver_shutdown(grpc_exec_ctx* exec_ctx,
                                   grpc_resolver* resolver) {
  fake_resolver* r = (fake_resolver*)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    grpc_exec_ctx_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE, NULL);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void fake_resolver_maybe_finish_next_locked(grpc_exec_ctx* exec_ctx,
                                                   fake_resolver* r) {
  if (r->next_completion != NULL && !r->published) {
    r->published = true;
    *r->target_result = grpc_resolver_result_create(
        r->target_name,
        grpc_lb_addresses_copy(r->addresses, NULL /* user_data_copy */),
        r->lb_policy_name, NULL /* lb_policy_args */);
    grpc_exec_ctx_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE, NULL);
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
                               grpc_resolver_result** target_result,
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

static grpc_resolver* fake_resolver_create(grpc_resolver_factory* factory,
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
  // Construct addresses.
  gpr_slice path_slice =
      gpr_slice_new(args->uri->path, strlen(args->uri->path), do_nothing);
  gpr_slice_buffer path_parts;
  gpr_slice_buffer_init(&path_parts);
  gpr_slice_split(path_slice, ",", &path_parts);
  grpc_lb_addresses* addresses = grpc_lb_addresses_create(path_parts.count);
  bool errors_found = false;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    grpc_uri ith_uri = *args->uri;
    char* part_str = gpr_dump_slice(path_parts.slices[i], GPR_DUMP_ASCII);
    ith_uri.path = part_str;
    if (!parse_ipv4(&ith_uri,
                    (struct sockaddr_storage*)(&addresses->addresses[i]
                                                    .address.addr),
                    &addresses->addresses[i].address.len)) {
      errors_found = true;
    }
    gpr_free(part_str);
    addresses->addresses[i].is_balancer = lb_enabled;
    if (errors_found) break;
  }
  gpr_slice_buffer_destroy(&path_parts);
  gpr_slice_unref(path_slice);
  if (errors_found) {
    grpc_lb_addresses_destroy(addresses, NULL /* user_data_destroy */);
    return NULL;
  }
  // Instantiate resolver.
  fake_resolver* r = gpr_malloc(sizeof(fake_resolver));
  memset(r, 0, sizeof(*r));
  r->target_name = gpr_strdup(args->uri->path);
  r->addresses = addresses;
  r->lb_policy_name =
      gpr_strdup(grpc_uri_get_query_arg(args->uri, "lb_policy"));
  gpr_mu_init(&r->mu);
  grpc_resolver_init(&r->base, &fake_resolver_vtable);
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
