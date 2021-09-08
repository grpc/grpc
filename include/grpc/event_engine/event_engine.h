// Copyright 2021 The gRPC Authors
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
#ifndef GRPC_EVENT_ENGINE_EVENT_ENGINE_H
#define GRPC_EVENT_ENGINE_EVENT_ENGINE_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/port.h>
#include <grpc/event_engine/slice_allocator.h>

// TODO(hork): Define the Endpoint::Write metrics collection system
namespace grpc_event_engine {
namespace experimental {

////////////////////////////////////////////////////////////////////////////////
/// The EventEngine encapsulates all platform-specific behaviors related to low
/// level network I/O, timers, asynchronous execution, and DNS resolution.
///
/// This interface allows developers to provide their own event management and
/// network stacks. Motivating uses cases for supporting custom EventEngines
/// include the ability to hook into external event loops, and using different
/// EventEngine instances for each channel to better insulate network I/O and
/// callback processing from other channels.
///
/// A default cross-platform EventEngine instance is provided by gRPC.
///
/// LIFESPAN AND OWNERSHIP
///
/// gRPC takes shared ownership of EventEngines via std::shared_ptrs to ensure
/// that the engines remain available until they are no longer needed. Depending
/// on the use case, engines may live until gRPC is shut down.
///
/// EXAMPLE USAGE (Not yet implemented)
///
/// Custom EventEngines can be specified per channel, and allow configuration
/// for both clients and servers. To set a custom EventEngine for a client
/// channel, you can do something like the following:
///
///    ChannelArguments args;
///    std::shared_ptr<EventEngine> engine = std::make_shared<MyEngine>(...);
///    args.SetEventEngine(engine);
///    MyAppClient client(grpc::CreateCustomChannel(
///        "localhost:50051", grpc::InsecureChannelCredentials(), args));
///
/// A gRPC server can use a custom EventEngine by calling the
/// ServerBuilder::SetEventEngine method:
///
///    ServerBuilder builder;
///    std::shared_ptr<EventEngine> engine = std::make_shared<MyEngine>(...);
///    builder.SetEventEngine(engine);
///    std::unique_ptr<Server> server(builder.BuildAndStart());
///    server->Wait();
///
////////////////////////////////////////////////////////////////////////////////
class EventEngine {
 public:
  /// Basic callable function. The first argument to all callbacks is an
  /// absl::Status indicating the status of the operation associated with this
  /// callback. Each EventEngine method that takes a callback parameter, defines
  /// the expected sets and meanings of statuses for that use case.
  using Callback = std::function<void(absl::Status)>;
  /// Callback handle, used to cancel a callback.
  struct TaskHandle {
    intptr_t keys[2];
  };
  /// Thin wrapper around a platform-specific sockaddr type. A sockaddr struct
  /// exists on all platforms that gRPC supports.
  ///
  /// Platforms are expected to provide definitions for:
  /// * sockaddr
  /// * sockaddr_in
  /// * sockaddr_in6
  class ResolvedAddress {
   public:
    static constexpr socklen_t MAX_SIZE_BYTES = 128;

    ResolvedAddress(const sockaddr* address, socklen_t size);
    ResolvedAddress() = default;
    ResolvedAddress(const ResolvedAddress&) = default;
    const struct sockaddr* address() const;
    socklen_t size() const;

   private:
    char address_[MAX_SIZE_BYTES];
    socklen_t size_ = 0;
  };

  /// One end of a connection between a gRPC client and server. Endpoints are
  /// created when connections are established, and Endpoint operations are
  /// gRPC's primary means of communication.
  ///
  /// Endpoints must use the provided SliceAllocator for all data buffer memory
  /// allocations. gRPC allows applications to set memory constraints per
  /// Channel or Server, and the implementation depends on all dynamic memory
  /// allocation being handled by the quota system.
  class Endpoint {
   public:
    /// Shuts down all connections and invokes all pending read or write
    /// callbacks with an error status.
    virtual ~Endpoint() = default;
    /// Reads data from the Endpoint.
    ///
    /// When data is available on the connection, that data is moved into the
    /// \a buffer, and the \a on_read callback is called. The caller must ensure
    /// that the callback has access to the buffer when executed later.
    /// Ownership of the buffer is not transferred. Valid slices *may* be placed
    /// into the buffer even if the callback is invoked with a non-OK Status.
    ///
    /// There can be at most one outstanding read per Endpoint at any given
    /// time. An outstanding read is one in which the \a on_read callback has
    /// not yet been executed for some previous call to \a Read.  If an attempt
    /// is made to call \a Read while a previous read is still outstanding, the
    /// \a EventEngine must abort.
    ///
    /// For failed read operations, implementations should pass the appropriate
    /// statuses to \a on_read. For example, callbacks might expect to receive
    /// CANCELLED on endpoint shutdown.
    virtual void Read(Callback on_read, SliceBuffer* buffer) = 0;
    /// Writes data out on the connection.
    ///
    /// \a on_writable is called when the connection is ready for more data. The
    /// Slices within the \a data buffer may be mutated at will by the Endpoint
    /// until \a on_writable is called. The \a data SliceBuffer will remain
    /// valid after calling \a Write, but its state is otherwise undefined.  All
    /// bytes in \a data must have been written before calling \a on_writable
    /// unless an error has occurred.
    ///
    /// There can be at most one outstanding write per Endpoint at any given
    /// time. An outstanding write is one in which the \a on_writable callback
    /// has not yet been executed for some previous call to \a Write.  If an
    /// attempt is made to call \a Write while a previous write is still
    /// outstanding, the \a EventEngine must abort.
    ///
    /// For failed write operations, implementations should pass the appropriate
    /// statuses to \a on_writable. For example, callbacks might expect to
    /// receive CANCELLED on endpoint shutdown.
    virtual void Write(Callback on_writable, SliceBuffer* data) = 0;
    /// Returns an address in the format described in DNSResolver. The returned
    /// values are expected to remain valid for the life of the Endpoint.
    virtual const ResolvedAddress& GetPeerAddress() const = 0;
    virtual const ResolvedAddress& GetLocalAddress() const = 0;
  };

  /// Called when a new connection is established.
  ///
  /// If the connection attempt was not successful, implementations should pass
  /// the appropriate statuses to this callback. For example, callbacks might
  /// expect to receive DEADLINE_EXCEEDED statuses when appropriate, or
  /// CANCELLED statuses on EventEngine shutdown.
  using OnConnectCallback =
      std::function<void(absl::StatusOr<std::unique_ptr<Endpoint>>)>;

  /// Listens for incoming connection requests from gRPC clients and initiates
  /// request processing once connections are established.
  class Listener {
   public:
    /// Called when the listener has accepted a new client connection.
    using AcceptCallback = std::function<void(
        std::unique_ptr<Endpoint>, const SliceAllocator& slice_allocator)>;
    virtual ~Listener() = default;
    /// Bind an address/port to this Listener.
    ///
    /// It is expected that multiple addresses/ports can be bound to this
    /// Listener before Listener::Start has been called. Returns either the
    /// bound port or an appropriate error status.
    virtual absl::StatusOr<int> Bind(const ResolvedAddress& addr) = 0;
    virtual absl::Status Start() = 0;
  };

  /// Factory method to create a network listener / server.
  ///
  /// Once a \a Listener is created and started, the \a on_accept callback will
  /// be called once asynchronously for each established connection. This method
  /// may return a non-OK status immediately if an error was encountered in any
  /// synchronous steps required to create the Listener. In this case,
  /// \a on_shutdown will never be called.
  ///
  /// If this method returns a Listener, then \a on_shutdown will be invoked
  /// exactly once, when the Listener is shut down. The status passed to it will
  /// indicate if there was a problem during shutdown.
  ///
  /// The provided \a SliceAllocatorFactory is used to create \a SliceAllocators
  /// for Endpoint construction.
  virtual absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept, Callback on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<SliceAllocatorFactory> slice_allocator_factory) = 0;
  /// Creates a client network connection to a remote network listener.
  ///
  /// May return an error status immediately if there was a failure in the
  /// synchronous part of establishing a connection. In that event, the \a
  /// on_connect callback *will not* have been executed. Otherwise, it is
  /// expected that the \a on_connect callback will be asynchronously executed
  /// exactly once by the EventEngine.
  ///
  /// Implementation Note: it is important that the \a slice_allocator be used
  /// for all read/write buffer allocations in the EventEngine implementation.
  /// This allows gRPC's \a ResourceQuota system to monitor and control memory
  /// usage with graceful degradation mechanisms. Please see the \a
  /// SliceAllocator API for more information.
  virtual absl::Status Connect(OnConnectCallback on_connect,
                               const ResolvedAddress& addr,
                               const EndpointConfig& args,
                               std::unique_ptr<SliceAllocator> slice_allocator,
                               absl::Time deadline) = 0;

  /// Provides asynchronous resolution.
  class DNSResolver {
   public:
    /// Task handle for DNS Resolution requests.
    struct LookupTaskHandle {
      intptr_t key[2];
    };
    /// DNS SRV record type.
    struct SRVRecord {
      std::string host;
      int port = 0;
      int priority = 0;
      int weight = 0;
    };
    /// Called with the collection of sockaddrs that were resolved from a given
    /// target address.
    using LookupHostnameCallback =
        std::function<void(absl::StatusOr<std::vector<ResolvedAddress>>)>;
    /// Called with a collection of SRV records.
    using LookupSRVCallback =
        std::function<void(absl::StatusOr<std::vector<SRVRecord>>)>;
    /// Called with the result of a TXT record lookup
    using LookupTXTCallback = std::function<void(absl::StatusOr<std::string>)>;

    virtual ~DNSResolver() = default;

    /// Asynchronously resolve an address.
    ///
    /// \a default_port may be a non-numeric named service port, and will only
    /// be used if \a address does not already contain a port component.
    ///
    /// When the lookup is complete, the \a on_resolve callback will be invoked
    /// with a status indicating the success or failure of the lookup.
    /// Implementations should pass the appropriate statuses to the callback.
    /// For example, callbacks might expect to receive DEADLINE_EXCEEDED when
    /// the deadline is exceeded or CANCELLED if the lookup was cancelled.
    virtual LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                            absl::string_view address,
                                            absl::string_view default_port,
                                            absl::Time deadline) = 0;
    /// Asynchronously perform an SRV record lookup.
    ///
    /// \a on_resolve has the same meaning and expectations as \a
    /// LookupHostname's \a on_resolve callback.
    virtual LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                                       absl::string_view name,
                                       absl::Time deadline) = 0;
    /// Asynchronously perform a TXT record lookup.
    ///
    /// \a on_resolve has the same meaning and expectations as \a
    /// LookupHostname's \a on_resolve callback.
    virtual LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                                       absl::string_view name,
                                       absl::Time deadline) = 0;
    /// Cancel an asynchronous lookup operation.
    virtual void TryCancelLookup(LookupTaskHandle handle) = 0;
  };

  /// At time of destruction, the EventEngine must have no active
  /// responsibilities. EventEngine users (applications) are responsible for
  /// cancelling all tasks and DNS lookups, shutting down listeners and
  /// endpoints, prior to EventEngine destruction. If there are any outstanding
  /// tasks, any running listeners, etc. at time of EventEngine destruction,
  /// that is an invalid use of the API, and it will result in undefined
  /// behavior.
  virtual ~EventEngine() = default;

  // TODO(nnoble): consider whether we can remove this method before we
  // de-experimentalize this API.
  virtual bool IsWorkerThread() = 0;

  /// Creates and returns an instance of a DNSResolver.
  virtual std::unique_ptr<DNSResolver> GetDNSResolver() = 0;

  /// Executes a callback as soon as possible.
  ///
  /// The \a fn callback's \a status argument is used to indicate whether it was
  /// executed normally. For example, the status may be CANCELLED if the
  /// EventEngine is being shut down. \a fn is guaranteed to be called exactly
  /// once.
  virtual void Run(Callback fn) = 0;
  /// Synonymous with scheduling an alarm to run at time \a when.
  ///
  /// The callback \a fn will execute when either when time \a when arrives
  /// (receiving status OK), or when the \a fn is cancelled (receiving status
  /// CANCELLED). The callback is guaranteed to be called exactly once.
  virtual TaskHandle RunAt(absl::Time when, Callback fn) = 0;
  /// Attempts to cancel a callback.
  /// Note that this is a "best effort" cancellation. No guarantee is made that
  /// the callback will be cancelled, the call could be in any stage.
  ///
  /// There are three scenarios in which we may cancel a scheduled task:
  ///   1. We cancel the execution before it has run.
  ///   2. The callback has already run.
  ///   3. We can't cancel it because it is "in flight".
  ///
  /// In all cases, the cancellation is still considered successful, the
  /// callback will be run exactly once from either cancellation or from its
  /// activation.
  virtual void TryCancel(TaskHandle handle) = 0;
};

// TODO(hork): finalize the API and document it. We need to firm up the story
// around user-provided EventEngines.
std::shared_ptr<EventEngine> DefaultEventEngineFactory();

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_EVENT_ENGINE_H
