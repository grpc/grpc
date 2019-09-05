/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONNECTOR_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/transport/transport.h"

typedef struct grpc_connector grpc_connector;
typedef struct grpc_connector_vtable grpc_connector_vtable;

struct grpc_connector {
  const grpc_connector_vtable* vtable;
};

typedef struct {
  /** set of pollsets interested in this connection */
  grpc_pollset_set* interested_parties;
  /** deadline for connection */
  grpc_millis deadline;
  /** channel arguments (to be passed to transport) */
  const grpc_channel_args* channel_args;
} grpc_connect_in_args;

typedef struct {
  /** the connected transport */
  grpc_transport* transport;

  /** channel arguments (to be passed to the filters) */
  grpc_channel_args* channel_args;

  /** channelz socket node of the connected transport. nullptr if not available
   */
  grpc_core::RefCountedPtr<grpc_core::channelz::SocketNode> socket;

  void reset() {
    transport = nullptr;
    channel_args = nullptr;
    socket = nullptr;
  }
} grpc_connect_out_args;

struct grpc_connector_vtable {
  void (*ref)(grpc_connector* connector);
  void (*unref)(grpc_connector* connector);
  /** Implementation of grpc_connector_shutdown */
  void (*shutdown)(grpc_connector* connector, grpc_error* why);
  /** Implementation of grpc_connector_connect */
  void (*connect)(grpc_connector* connector,
                  const grpc_connect_in_args* in_args,
                  grpc_connect_out_args* out_args, grpc_closure* notify);
};

grpc_connector* grpc_connector_ref(grpc_connector* connector);
void grpc_connector_unref(grpc_connector* connector);
/** Connect using the connector: max one outstanding call at a time */
void grpc_connector_connect(grpc_connector* connector,
                            const grpc_connect_in_args* in_args,
                            grpc_connect_out_args* out_args,
                            grpc_closure* notify);
/** Cancel any pending connection */
void grpc_connector_shutdown(grpc_connector* connector, grpc_error* why);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_CONNECTOR_H */
