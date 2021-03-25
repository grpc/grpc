/*
 *
 * Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_EVENT_ENGINE_EVENT_ENGINE_H
#define GRPC_CORE_LIB_EVENT_ENGINE_EVENT_ENGINE_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "grpc/event_engine/slice_allocator.h"
#include "grpc/event_engine/sockaddr.h"

// TODO(hork): explicitly define lifetimes and ownership of all objects.
// TODO(hork): Define the Endpoint::Write metrics collection system
// TODO(hork): Add minimal tests
// TODO(hork): Add mechanism to set an EventEngine per Channel

namespace grpc_io {
namespace experimental {

////////////////////////////////////////////////////////////////////////////////
// The EventEngine encapsulates all platform-specific behaviors related to low
// level network I/O, timers, asynchronous execution, and DNS resolution.
//
// This interface allows developers to provide their own event management and
// network stacks. Motivating uses cases for supporting custom EventEngines
// include the ability to hook into external event loops, and using different
// EventEngine instances for each channel to better insulate network I/O and
// callback processing from other channels.
//
// A default cross-platform EventEngine instance is provided by gRPC.
//
// LIFESPAN AND OWNERSHIP
//
// gRPC takes shared ownership of EventEngines via std::shared_ptrs to ensure
// that the engines remain available until they are no longer needed. Depending
// on the use case, engines may live until gRPC is shut down.
//
// EXAMPLE USAGE
//
// Custom EventEngines can be specified per channel, and allow configuration
// for both clients and servers. To set a custom EventEngine for a client
// channel, you can do something like the following:
//
//    ChannelArguments args;
//    std::shared_ptr<EventEngine> engine = std::make_shared<MyEngine>(...);
//    args.SetEventEngine(engine);
//    MyAppClient client(grpc::CreateCustomChannel(
//        "localhost:50051", grpc::InsecureChannelCredentials(), args));
//
// A gRPC server can be set using the ServerBuild::SetEventEngine method:
//
//    ServerBuilder builder;
//    std::shared_ptr<EventEngine> engine = std::make_shared<MyEngine>(...);
//    builder.SetEventEngine(engine);
//    std::unique_ptr<Server> server(builder.BuildAndStart());
//    server->Wait();
//
////////////////////////////////////////////////////////////////////////////////
class EventEngine {
 public:
  // A basic callable function. The first argument to all callbacks is an
  // absl::Status indicating the status of the operation associated with this
  // callback.
  //
  // All callback types share these common absl::Status meanings:
  // * `StatusCode::kOk`: The callback is executed normally, no errors were
  // encountered.
  // * `StatusCode::kCancelled`: The callback was explicitly cancelled via the
  // EventEngine API.
  // * `StatusCode::kDeadlineExceeded`: Deadline is exceeded. Deadlines are
  // available for some task scheduling operations.
  using Callback = std::function<void(absl::Status)>;
  // A callback handle, used to cancel a callback.
  struct TaskHandle {
    intptr_t key;
  };

  // An Endpoint represents one end of a connection between a gRPC client
  // and server. Endpoints are created when connections are established, and
  // Endpoint operations are gRPC's primary means of communication.
  //
  // Endpoints must use the provided SliceAllocator for all data buffer memory
  // allocations. gRPC allows applications to set memory constraints per Channel
  // or Server, and the implementation depends on all dynamic memory allocation
  // being handled by the quota system.
  class Endpoint {
   public:
    // Called when a new connection is established. This callback takes
    // ownership of the Endpoint and is responsible for its destruction.
    using OnConnectCallback = std::function<void(absl::Status, Endpoint*)>;

    virtual ~Endpoint() = 0;

    // Read data from the Endpoint.
    //
    // When data is available on the connection, that data is moved into the
    // `buffer`, and the `on_read` callback is called. The caller must ensure
    // that the callback has access to the `buffer` when executed later.
    // Ownership of `buffer` is not transferred. Valid slices *may* be placed
    // into the `buffer` even if the callback is invoked with Status != OK.
    virtual void Read(Callback on_read, SliceBuffer* buffer,
                      absl::Time deadline) = 0;
    // Write data out on the connection.
    //
    // `on_writable` is called when the connection is ready for more data. The
    // SliceBuffer will remain valid after calling `Endpoint::Write`, but its
    // state is otherwise undefined.
    virtual void Write(Callback on_writable, SliceBuffer* data,
                       absl::Time deadline) = 0;
    // TODO(hork): define cleanup operations, lifetimes, responsibilities.
    virtual void Close(Callback on_close) = 0;
    // These methods return an address in the format described in DNSResolver.
    // The returned values are owned by the Endpoint and are expected to remain
    // valid for the life of the Endpoint.
    virtual const ResolvedAddress* GetPeerAddress() const = 0;
    virtual const ResolvedAddress* GetLocalAddress() const = 0;
  };

  // An EventEngine Listener listens for incoming connection requests from gRPC
  // clients and initiates request processing once connections are established.
  class Listener {
   public:
    // Called when the listener has accepted a new client connection. This
    // callback takes ownership of the Endpoint and is responsible its
    // destruction.
    using AcceptCallback = std::function<void(absl::Status, Endpoint*)>;

    virtual ~Listener() = 0;

    // Bind an address/port to this Listener. It is expected that multiple
    // addresses/ports can be bound to this Listener before Listener::Start has
    // been called.
    // TODO(hork): requires output port argument, return value, or callback
    virtual absl::Status Bind(const ResolvedAddress& addr) = 0;
    virtual absl::Status Start() = 0;
    virtual absl::Status Shutdown() = 0;
  };

  // Factory method to create a network listener.
  virtual absl::StatusOr<Listener> CreateListener(
      Listener::AcceptCallback on_accept, Callback on_shutdown,
      const ChannelArgs& args,
      SliceAllocatorFactory slice_allocator_factory) = 0;
  // Creates a network connection to a remote network listener.
  virtual absl::Status Connect(Endpoint::OnConnectCallback on_connect,
                               const ResolvedAddress& addr,
                               const ChannelArgs& args,
                               SliceAllocator slice_allocator,
                               absl::Time deadline) = 0;

  // The DNSResolver that provides asynchronous resolution.
  class DNSResolver {
   public:
    // A task handle for DNS Resolution requests.
    struct LookupTaskHandle {
      intptr_t key;
    };
    // A DNS SRV record type.
    struct SRVRecord {
      std::string host;
      int port = 0;
      int priority = 0;
      int weight = 0;
    };
    // Called with the collection of sockaddrs that were resolved from a given
    // target address.
    using LookupHostnameCallback =
        std::function<void(absl::Status, std::vector<ResolvedAddress>)>;
    // Called with a collection of SRV records.
    using LookupSRVCallback =
        std::function<void(absl::Status, std::vector<SRVRecord>)>;
    using LookupTXTCallback = std::function<void(absl::Status, std::string)>;

    virtual ~DNSResolver() = 0;

    // Asynchronously resolve an address. `default_port` may be a non-numeric
    // named service port, and will only be used if `address` does not already
    // contain a port component.
    virtual LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                            absl::string_view address,
                                            absl::string_view default_port,
                                            absl::Time deadline) = 0;
    virtual LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                                       absl::string_view name,
                                       absl::Time deadline) = 0;
    virtual LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                                       absl::string_view name,
                                       absl::Time deadline) = 0;
    // Cancel an asynchronous lookup operation.
    virtual void TryCancelLookup(LookupTaskHandle handle) = 0;
  };

  virtual ~EventEngine() = 0;

  // Retrieves an instance of a DNSResolver.
  virtual absl::StatusOr<DNSResolver> GetDNSResolver() = 0;

  // Intended for future expansion of Task run functionality.
  struct RunOptions {};
  // Run a callback as soon as possible.
  // TODO(hork): consider recommendation to make TaskHandle an output arg
  virtual TaskHandle Run(Callback fn, RunOptions opts) = 0;
  // Synonymous with scheduling an alarm to run at time `when`.
  virtual TaskHandle RunAt(absl::Time when, Callback fn, RunOptions opts) = 0;
  // Immediately tries to cancel a callback.
  // Note that this is a "best effort" cancellation. No guarantee is made that
  // the callback will be cancelled, the call could be in any stage.
  //
  // There are three scenarios in which we may cancel a scheduled function:
  //   1. We cancel the execution before it has run.
  //   2. The callback has already run.
  //   3. We can't cancel it because it is "in flight".
  //
  // In all cases, the cancellation is still considered successful, the callback
  // will be run exactly once from either cancellation or from its activation.
  virtual void TryCancel(TaskHandle handle) = 0;
  // Immediately run all callbacks with status indicating the shutdown
  virtual absl::Status Shutdown() = 0;
};

// A blocking version of DNS resoltuion. This is implemented in terms of the
// async EventEngine lookup, and so is less efficient. Prefer the async version
// unless blocking resolution is required.
absl::StatusOr<std::vector<ResolvedAddress>> BlockingLookupHostname(
    std::shared_ptr<EventEngine> engine, absl::string_view address,
    absl::string_view default_port, absl::Time deadline);

// Lazily instantiate and return a default global EventEngine instance if no
// custom instance is provided. If a custom EventEngine is provided for every
// channel/server via ChannelArgs, the default instance will never be
// instantiated.
std::shared_ptr<EventEngine> grpc_get_default_event_engine();

}  // namespace experimental
}  // namespace grpc_io

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_EVENT_ENGINE_H
