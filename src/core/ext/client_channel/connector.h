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

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_CONNECTOR_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_CONNECTOR_H

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/transport/transport.h"

typedef struct grpc_connector grpc_connector;
typedef struct grpc_connector_vtable grpc_connector_vtable;

struct grpc_connector {
  const grpc_connector_vtable *vtable;
};

typedef struct {
  /** set of pollsets interested in this connection */
  grpc_pollset_set *interested_parties;
  /** initial connect string to send */
  grpc_slice initial_connect_string;
  /** deadline for connection */
  gpr_timespec deadline;
  /** channel arguments (to be passed to transport) */
  const grpc_channel_args *channel_args;
} grpc_connect_in_args;

typedef struct {
  /** the connected transport */
  grpc_transport *transport;

  /** channel arguments (to be passed to the filters) */
  grpc_channel_args *channel_args;
} grpc_connect_out_args;

struct grpc_connector_vtable {
  void (*ref)(grpc_connector *connector);
  void (*unref)(grpc_exec_ctx *exec_ctx, grpc_connector *connector);
  /** Implementation of grpc_connector_shutdown */
  void (*shutdown)(grpc_exec_ctx *exec_ctx, grpc_connector *connector,
                   grpc_error *why);
  /** Implementation of grpc_connector_connect */
  void (*connect)(grpc_exec_ctx *exec_ctx, grpc_connector *connector,
                  const grpc_connect_in_args *in_args,
                  grpc_connect_out_args *out_args, grpc_closure *notify);
};

grpc_connector *grpc_connector_ref(grpc_connector *connector);
void grpc_connector_unref(grpc_exec_ctx *exec_ctx, grpc_connector *connector);
/** Connect using the connector: max one outstanding call at a time */
void grpc_connector_connect(grpc_exec_ctx *exec_ctx, grpc_connector *connector,
                            const grpc_connect_in_args *in_args,
                            grpc_connect_out_args *out_args,
                            grpc_closure *notify);
/** Cancel any pending connection */
void grpc_connector_shutdown(grpc_exec_ctx *exec_ctx, grpc_connector *connector,
                             grpc_error *why);

#endif /* GRPC_CORE_EXT_CLIENT_CHANNEL_CONNECTOR_H */
