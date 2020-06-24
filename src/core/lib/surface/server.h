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

#ifndef GRPC_CORE_LIB_SURFACE_SERVER_H
#define GRPC_CORE_LIB_SURFACE_SERVER_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/transport/transport.h"

extern const grpc_channel_filter grpc_server_top_filter;

/** Lightweight tracing of server channel state */
extern grpc_core::TraceFlag grpc_server_channel_trace;

namespace grpc_core {

/// Interface for listeners.
/// Implementations must override the Orphan() method, which should stop
/// listening and initiate destruction of the listener.
class ServerListenerInterface : public Orphanable {
 public:
  virtual ~ServerListenerInterface() = default;

  /// Starts listening. This listener may refer to the pollset object beyond
  /// this call, so it is a pointer rather than a reference.
  virtual void Start(grpc_server* server,
                     const std::vector<grpc_pollset*>* pollsets) = 0;

  /// Returns the channelz node for the listen socket, or null if not
  /// supported.
  virtual channelz::ListenSocketNode* channelz_listen_socket_node() const = 0;

  /// Sets a closure to be invoked by the listener when its destruction
  /// is complete.
  virtual void SetOnDestroyDone(grpc_closure* on_destroy_done) = 0;
};

}  // namespace grpc_core

/* Add a listener to the server: when the server starts, it will call Start(),
   and when it shuts down, it will orphan the listener. */
void grpc_server_add_listener(
    grpc_server* server,
    grpc_core::OrphanablePtr<grpc_core::ServerListenerInterface> listener);

/* Setup a transport - creates a channel stack, binds the transport to the
   server */
void grpc_server_setup_transport(
    grpc_server* server, grpc_transport* transport,
    grpc_pollset* accepting_pollset, const grpc_channel_args* args,
    const grpc_core::RefCountedPtr<grpc_core::channelz::SocketNode>&
        socket_node,
    grpc_resource_user* resource_user = nullptr);

grpc_core::channelz::ServerNode* grpc_server_get_channelz_node(
    grpc_server* server);

const grpc_channel_args* grpc_server_get_channel_args(grpc_server* server);

grpc_resource_user* grpc_server_get_default_resource_user(grpc_server* server);

bool grpc_server_has_open_connections(grpc_server* server);

// Do not call this before grpc_server_start. Returns the pollsets. The vector
// itself is immutable, but the pollsets inside are mutable. The result is valid
// for the lifetime of the server.
const std::vector<grpc_pollset*>& grpc_server_get_pollsets(grpc_server* server);

namespace grpc_core {

// An object to represent the most relevant characteristics of a newly-allocated
// call object when using an AllocatingRequestMatcherBatch
struct ServerBatchCallAllocation {
  grpc_experimental_completion_queue_functor* tag;
  grpc_call** call;
  grpc_metadata_array* initial_metadata;
  grpc_call_details* details;
};

// An object to represent the most relevant characteristics of a newly-allocated
// call object when using an AllocatingRequestMatcherRegistered
struct ServerRegisteredCallAllocation {
  grpc_experimental_completion_queue_functor* tag;
  grpc_call** call;
  grpc_metadata_array* initial_metadata;
  gpr_timespec* deadline;
  grpc_byte_buffer** optional_payload;
};

// Functions to specify that a specific registered method or the unregistered
// collection should use a specific allocator for request matching.
void SetServerRegisteredMethodAllocator(
    grpc_server* server, grpc_completion_queue* cq, void* method_tag,
    std::function<ServerRegisteredCallAllocation()> allocator);
void SetServerBatchMethodAllocator(
    grpc_server* server, grpc_completion_queue* cq,
    std::function<ServerBatchCallAllocation()> allocator);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SURFACE_SERVER_H */
