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

#include "src/core/iomgr/sockaddr.h"

#include <grpc/grpc.h>

#include <stdlib.h>
#include <string.h>

#include "src/core/channel/census_filter.h"
#include "src/core/channel/channel_args.h"
#include "src/core/channel/client_channel.h"
#include "src/core/channel/client_setup.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/channel/http_client_filter.h"
#include "src/core/iomgr/resolve_address.h"
#include "src/core/iomgr/tcp_client.h"
#include "src/core/security/auth_filters.h"
#include "src/core/security/credentials.h"
#include "src/core/security/secure_transport_setup.h"
#include "src/core/support/string.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/client.h"
#include "src/core/transport/chttp2_transport.h"
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>
#include "src/core/tsi/transport_security_interface.h"

typedef struct setup setup;

/* A single setup request (started via initiate) */
typedef struct {
  grpc_client_setup_request *cs_request;
  setup *setup;
  /* Resolved addresses, or null if resolution not yet completed. */
  grpc_resolved_addresses *resolved;
  /* which address in resolved should we pick for the next connection attempt */
  size_t resolved_index;
} request;

struct setup {
  grpc_channel_security_connector *security_connector;
  const char *target;
  grpc_transport_setup_callback setup_callback;
  void *setup_user_data;
};

static int maybe_try_next_resolved(request *r);

static void done(request *r, int was_successful) {
  grpc_client_setup_request_finish(r->cs_request, was_successful);
  if (r->resolved) {
    grpc_resolved_addresses_destroy(r->resolved);
  }
  gpr_free(r);
}

static void on_secure_transport_setup_done(void *rp,
                                           grpc_security_status status,
                                           grpc_endpoint *secure_endpoint) {
  request *r = rp;
  if (status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Secure transport setup failed with error %d.", status);
    done(r, 0);
  } else if (grpc_client_setup_cb_begin(r->cs_request)) {
    grpc_create_chttp2_transport(
        r->setup->setup_callback, r->setup->setup_user_data,
        grpc_client_setup_get_channel_args(r->cs_request), secure_endpoint,
        NULL, 0, grpc_client_setup_get_mdctx(r->cs_request), 1);
    grpc_client_setup_cb_end(r->cs_request);
    done(r, 1);
  } else {
    done(r, 0);
  }
}

/* connection callback: tcp is either valid, or null on error */
static void on_connect(void *rp, grpc_endpoint *tcp) {
  request *r = rp;

  if (!grpc_client_setup_request_should_continue(r->cs_request)) {
    if (tcp) {
      grpc_endpoint_shutdown(tcp);
      grpc_endpoint_destroy(tcp);
    }
    done(r, 0);
    return;
  }

  if (!tcp) {
    if (!maybe_try_next_resolved(r)) {
      done(r, 0);
      return;
    } else {
      return;
    }
  } else {
    grpc_setup_secure_transport(&r->setup->security_connector->base, tcp,
                                on_secure_transport_setup_done, r);
  }
}

/* attempt to connect to the next available resolved address */
static int maybe_try_next_resolved(request *r) {
  grpc_resolved_address *addr;
  if (!r->resolved) return 0;
  if (r->resolved_index == r->resolved->naddrs) return 0;
  addr = &r->resolved->addrs[r->resolved_index++];
  grpc_tcp_client_connect(on_connect, r, (struct sockaddr *)&addr->addr,
                          addr->len,
                          grpc_client_setup_request_deadline(r->cs_request));
  return 1;
}

/* callback for when our target address has been resolved */
static void on_resolved(void *rp, grpc_resolved_addresses *resolved) {
  request *r = rp;

  /* if we're not still the active request, abort */
  if (!grpc_client_setup_request_should_continue(r->cs_request)) {
    if (resolved) {
      grpc_resolved_addresses_destroy(resolved);
    }
    done(r, 0);
    return;
  }

  if (!resolved) {
    done(r, 0);
    return;
  } else {
    r->resolved = resolved;
    r->resolved_index = 0;
    if (!maybe_try_next_resolved(r)) {
      done(r, 0);
    }
  }
}

static void initiate_setup(void *sp, grpc_client_setup_request *cs_request) {
  request *r = gpr_malloc(sizeof(request));
  r->setup = sp;
  r->cs_request = cs_request;
  r->resolved = NULL;
  r->resolved_index = 0;
  /* TODO(klempner): Make grpc_resolve_address respect deadline */
  grpc_resolve_address(r->setup->target, "https", on_resolved, r);
}

static void done_setup(void *sp) {
  setup *s = sp;
  gpr_free((void *)s->target);
  grpc_security_connector_unref(&s->security_connector->base);
  gpr_free(s);
}

static grpc_transport_setup_result complete_setup(void *channel_stack,
                                                  grpc_transport *transport,
                                                  grpc_mdctx *mdctx) {
  static grpc_channel_filter const *extra_filters[] = {
      &grpc_client_auth_filter, &grpc_http_client_filter};
  return grpc_client_channel_transport_setup_complete(
      channel_stack, transport, extra_filters, GPR_ARRAY_SIZE(extra_filters),
      mdctx);
}

/* Create a secure client channel:
   Asynchronously: - resolve target
                   - connect to it (trying alternatives as presented)
                   - perform handshakes */
grpc_channel *grpc_secure_channel_create(grpc_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args) {
  setup *s;
  grpc_channel *channel;
  grpc_arg connector_arg;
  grpc_channel_args *args_copy;
  grpc_channel_args *new_args_from_connector;
  grpc_channel_security_connector *connector;
  grpc_mdctx *mdctx;
#define MAX_FILTERS 3
  const grpc_channel_filter *filters[MAX_FILTERS];
  int n = 0;

  if (grpc_find_security_connector_in_args(args) != NULL) {
    gpr_log(GPR_ERROR, "Cannot set security context in channel args.");
    return grpc_lame_client_channel_create();
  }

  if (grpc_credentials_create_security_connector(
          creds, target, args, NULL, &connector, &new_args_from_connector) !=
      GRPC_SECURITY_OK) {
    return grpc_lame_client_channel_create();
  }
  mdctx = grpc_credentials_get_or_create_metadata_context(creds);

  s = gpr_malloc(sizeof(setup));
  connector_arg = grpc_security_connector_to_arg(&connector->base);
  args_copy = grpc_channel_args_copy_and_add(
      new_args_from_connector != NULL ? new_args_from_connector : args,
      &connector_arg);
  filters[n++] = &grpc_client_surface_filter;
  if (grpc_channel_args_is_census_enabled(args)) {
    filters[n++] = &grpc_client_census_filter;
  }
  filters[n++] = &grpc_client_channel_filter;
  GPR_ASSERT(n <= MAX_FILTERS);
  channel = grpc_channel_create_from_filters(filters, n, args_copy, mdctx, 1);
  grpc_channel_args_destroy(args_copy);
  if (new_args_from_connector != NULL) {
    grpc_channel_args_destroy(new_args_from_connector);
  }

  s->target = gpr_strdup(target);
  s->setup_callback = complete_setup;
  s->setup_user_data = grpc_channel_get_channel_stack(channel);
  s->security_connector = connector;
  grpc_client_setup_create_and_attach(grpc_channel_get_channel_stack(channel),
                                      args, mdctx, initiate_setup, done_setup,
                                      s);
  return channel;
}
