/*
 *
 * Copyright 2015-2016, Google Inc.
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
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** the addresses that we've 'resolved' */
  grpc_lb_addresses *addresses;
  /** channel args */
  grpc_channel_args *channel_args;
  /** have we published? */
  bool published;
  /** pending next completion, or NULL */
  grpc_closure *next_completion;
  /** target result address for next completion */
  grpc_channel_args **target_result;
} sockaddr_resolver;

static void sockaddr_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *r);

static void sockaddr_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              sockaddr_resolver *r);

static void sockaddr_shutdown_locked(grpc_exec_ctx *exec_ctx, grpc_resolver *r);
static void sockaddr_channel_saw_error_locked(grpc_exec_ctx *exec_ctx,
                                              grpc_resolver *r);
static void sockaddr_next_locked(grpc_exec_ctx *exec_ctx, grpc_resolver *r,
                                 grpc_channel_args **target_result,
                                 grpc_closure *on_complete);

static const grpc_resolver_vtable sockaddr_resolver_vtable = {
    sockaddr_destroy, sockaddr_shutdown_locked,
    sockaddr_channel_saw_error_locked, sockaddr_next_locked};

static void sockaddr_shutdown_locked(grpc_exec_ctx *exec_ctx,
                                     grpc_resolver *resolver) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  if (r->next_completion != NULL) {
    *r->target_result = NULL;
    grpc_closure_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
  }
}

static void sockaddr_channel_saw_error_locked(grpc_exec_ctx *exec_ctx,
                                              grpc_resolver *resolver) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  r->published = false;
  sockaddr_maybe_finish_next_locked(exec_ctx, r);
}

static void sockaddr_next_locked(grpc_exec_ctx *exec_ctx,
                                 grpc_resolver *resolver,
                                 grpc_channel_args **target_result,
                                 grpc_closure *on_complete) {
  sockaddr_resolver *r = (sockaddr_resolver *)resolver;
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_result = target_result;
  sockaddr_maybe_finish_next_locked(exec_ctx, r);
}

static void sockaddr_maybe_finish_next_locked(grpc_exec_ctx *exec_ctx,
                                              sockaddr_resolver *r) {
  if (r->next_completion != NULL && !r->published) {
    r->published = true;
    grpc_arg arg = grpc_lb_addresses_create_channel_arg(r->addresses);
    *r->target_result =
        grpc_channel_args_copy_and_add(r->channel_args, &arg, 1);
    grpc_closure_sched(exec_ctx, r->next_completion, GRPC_ERROR_NONE);
    r->next_completion = NULL;
  }
}

static void sockaddr_destroy(grpc_exec_ctx *exec_ctx, grpc_resolver *gr) {
  sockaddr_resolver *r = (sockaddr_resolver *)gr;
  grpc_lb_addresses_destroy(exec_ctx, r->addresses);
  grpc_channel_args_destroy(exec_ctx, r->channel_args);
  gpr_free(r);
}

static char *ip_get_default_authority(grpc_uri *uri) {
  const char *path = uri->path;
  if (path[0] == '/') ++path;
  return gpr_strdup(path);
}

static char *ipv4_get_default_authority(grpc_resolver_factory *factory,
                                        grpc_uri *uri) {
  return ip_get_default_authority(uri);
}

static char *ipv6_get_default_authority(grpc_resolver_factory *factory,
                                        grpc_uri *uri) {
  return ip_get_default_authority(uri);
}

#ifdef GRPC_HAVE_UNIX_SOCKET
char *unix_get_default_authority(grpc_resolver_factory *factory,
                                 grpc_uri *uri) {
  return gpr_strdup("localhost");
}
#endif

static void do_nothing(void *ignored) {}

static grpc_resolver *sockaddr_create(grpc_exec_ctx *exec_ctx,
                                      grpc_resolver_args *args,
                                      bool parse(const grpc_uri *uri,
                                                 grpc_resolved_address *dst)) {
  if (0 != strcmp(args->uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based uri's not supported by the %s scheme",
            args->uri->scheme);
    return NULL;
  }
  /* Construct addresses. */
  grpc_slice path_slice =
      grpc_slice_new(args->uri->path, strlen(args->uri->path), do_nothing);
  grpc_slice_buffer path_parts;
  grpc_slice_buffer_init(&path_parts);
  grpc_slice_split(path_slice, ",", &path_parts);
  grpc_lb_addresses *addresses =
      grpc_lb_addresses_create(path_parts.count, NULL /* user_data_vtable */);
  bool errors_found = false;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    grpc_uri ith_uri = *args->uri;
    char *part_str = grpc_slice_to_c_string(path_parts.slices[i]);
    ith_uri.path = part_str;
    if (!parse(&ith_uri, &addresses->addresses[i].address)) {
      errors_found = true; /* GPR_TRUE */
    }
    gpr_free(part_str);
    if (errors_found) break;
  }
  grpc_slice_buffer_destroy_internal(exec_ctx, &path_parts);
  grpc_slice_unref_internal(exec_ctx, path_slice);
  if (errors_found) {
    grpc_lb_addresses_destroy(exec_ctx, addresses);
    return NULL;
  }
  /* Instantiate resolver. */
  sockaddr_resolver *r = gpr_zalloc(sizeof(sockaddr_resolver));
  r->addresses = addresses;
  r->channel_args = grpc_channel_args_copy(args->args);
  grpc_resolver_init(&r->base, &sockaddr_resolver_vtable, args->combiner);
  return &r->base;
}

/*
 * FACTORY
 */

static void sockaddr_factory_ref(grpc_resolver_factory *factory) {}

static void sockaddr_factory_unref(grpc_resolver_factory *factory) {}

#define DECL_FACTORY(name)                                                  \
  static grpc_resolver *name##_factory_create_resolver(                     \
      grpc_exec_ctx *exec_ctx, grpc_resolver_factory *factory,              \
      grpc_resolver_args *args) {                                           \
    return sockaddr_create(exec_ctx, args, grpc_parse_##name);              \
  }                                                                         \
  static const grpc_resolver_factory_vtable name##_factory_vtable = {       \
      sockaddr_factory_ref, sockaddr_factory_unref,                         \
      name##_factory_create_resolver, name##_get_default_authority, #name}; \
  static grpc_resolver_factory name##_resolver_factory = {                  \
      &name##_factory_vtable}

#ifdef GRPC_HAVE_UNIX_SOCKET
DECL_FACTORY(unix);
#endif
DECL_FACTORY(ipv4);
DECL_FACTORY(ipv6);

void grpc_resolver_sockaddr_init(void) {
  grpc_register_resolver_type(&ipv4_resolver_factory);
  grpc_register_resolver_type(&ipv6_resolver_factory);
#ifdef GRPC_HAVE_UNIX_SOCKET
  grpc_register_resolver_type(&unix_resolver_factory);
#endif
}

void grpc_resolver_sockaddr_shutdown(void) {}
