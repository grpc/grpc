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

#include <grpc/support/port_platform.h>
#ifdef GPR_POSIX_SOCKET

#include "src/core/client_config/resolvers/unix_resolver_posix.h"

#include <string.h>
#include <sys/un.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/client_config/lb_policies/pick_first.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/support/string.h"

typedef struct {
  /** base class: must be first */
  grpc_resolver base;
  /** refcount */
  gpr_refcount refs;
  /** subchannel factory */
  grpc_subchannel_factory *subchannel_factory;
  /** load balancing policy factory */
  grpc_lb_policy *(*lb_policy_factory)(grpc_subchannel **subchannels,
                                       size_t num_subchannels);

  /** the address that we've 'resolved' */
  struct sockaddr_un addr;
  int addr_len;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** have we published? */
  int published;
  /** pending next completion, or NULL */
  grpc_iomgr_closure *next_completion;
  /** target config address for next completion */
  grpc_client_config **target_config;
} unix_resolver;

static void unix_destroy(unix_resolver *r);

static void unix_maybe_finish_next_locked(unix_resolver *r);

static void unix_ref(grpc_resolver *r);
static void unix_unref(grpc_resolver *r);
static void unix_shutdown(grpc_resolver *r);
static void unix_channel_saw_error(grpc_resolver *r,
                                   struct sockaddr *failing_address,
                                   int failing_address_len);
static void unix_next(grpc_resolver *r, grpc_client_config **target_config,
                      grpc_iomgr_closure *on_complete);

static const grpc_resolver_vtable unix_resolver_vtable = {
    unix_ref, unix_unref, unix_shutdown, unix_channel_saw_error, unix_next};

static void unix_ref(grpc_resolver *resolver) {
  unix_resolver *r = (unix_resolver *)resolver;
  gpr_ref(&r->refs);
}

static void unix_unref(grpc_resolver *resolver) {
  unix_resolver *r = (unix_resolver *)resolver;
  if (gpr_unref(&r->refs)) {
    unix_destroy(r);
  }
}

static void unix_shutdown(grpc_resolver *resolver) {
  unix_resolver *r = (unix_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  if (r->next_completion != NULL) {
    *r->target_config = NULL;
    /* TODO(ctiller): add delayed callback */
    grpc_iomgr_add_callback(r->next_completion);
    r->next_completion = NULL;
  }
  gpr_mu_unlock(&r->mu);
}

static void unix_channel_saw_error(grpc_resolver *resolver, struct sockaddr *sa,
                                   int len) {}

static void unix_next(grpc_resolver *resolver,
                      grpc_client_config **target_config,
                      grpc_iomgr_closure *on_complete) {
  unix_resolver *r = (unix_resolver *)resolver;
  gpr_mu_lock(&r->mu);
  GPR_ASSERT(!r->next_completion);
  r->next_completion = on_complete;
  r->target_config = target_config;
  unix_maybe_finish_next_locked(r);
  gpr_mu_unlock(&r->mu);
}

static void unix_maybe_finish_next_locked(unix_resolver *r) {
  grpc_client_config *cfg;
  grpc_lb_policy *lb_policy;
  grpc_subchannel *subchannel;
  grpc_subchannel_args args;

  if (r->next_completion != NULL && !r->published) {
    cfg = grpc_client_config_create();
    memset(&args, 0, sizeof(args));
    args.addr = (struct sockaddr *)&r->addr;
    args.addr_len = r->addr_len;
    subchannel =
        grpc_subchannel_factory_create_subchannel(r->subchannel_factory, &args);
    lb_policy = r->lb_policy_factory(&subchannel, 1);
    grpc_client_config_set_lb_policy(cfg, lb_policy);
    GRPC_LB_POLICY_UNREF(lb_policy, "unix");
    r->published = 1;
    *r->target_config = cfg;
    grpc_iomgr_add_callback(r->next_completion);
    r->next_completion = NULL;
  }
}

static void unix_destroy(unix_resolver *r) {
  gpr_mu_destroy(&r->mu);
  grpc_subchannel_factory_unref(r->subchannel_factory);
  gpr_free(r);
}

static grpc_resolver *unix_create(
    grpc_uri *uri,
    grpc_lb_policy *(*lb_policy_factory)(grpc_subchannel **subchannels,
                                         size_t num_subchannels),
    grpc_subchannel_factory *subchannel_factory) {
  unix_resolver *r;

  if (0 != strcmp(uri->authority, "")) {
    gpr_log(GPR_ERROR, "authority based uri's not supported");
    return NULL;
  }

  r = gpr_malloc(sizeof(unix_resolver));
  memset(r, 0, sizeof(*r));
  gpr_ref_init(&r->refs, 1);
  gpr_mu_init(&r->mu);
  r->base.vtable = &unix_resolver_vtable;
  r->subchannel_factory = subchannel_factory;
  r->lb_policy_factory = lb_policy_factory;

  r->addr.sun_family = AF_UNIX;
  strcpy(r->addr.sun_path, uri->path);
  r->addr_len = strlen(r->addr.sun_path) + sizeof(r->addr.sun_family) + 1;

  grpc_subchannel_factory_ref(subchannel_factory);
  return &r->base;
}

/*
 * FACTORY
 */

static void unix_factory_ref(grpc_resolver_factory *factory) {}

static void unix_factory_unref(grpc_resolver_factory *factory) {}

static grpc_resolver *unix_factory_create_resolver(
    grpc_resolver_factory *factory, grpc_uri *uri,
    grpc_subchannel_factory *subchannel_factory) {
  return unix_create(uri, grpc_create_pick_first_lb_policy, subchannel_factory);
}

static const grpc_resolver_factory_vtable unix_factory_vtable = {
    unix_factory_ref, unix_factory_unref, unix_factory_create_resolver};
static grpc_resolver_factory unix_resolver_factory = {&unix_factory_vtable};

grpc_resolver_factory *grpc_unix_resolver_factory_create() {
  return &unix_resolver_factory;
}

#endif
