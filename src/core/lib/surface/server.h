//
// Copyright 2015 gRPC authors.
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
//

#ifndef GRPC_CORE_LIB_SURFACE_SERVER_H
#define GRPC_CORE_LIB_SURFACE_SERVER_H

#include <grpc/support/port_platform.h>

#include <list>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/atomic.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

extern TraceFlag grpc_server_channel_trace;

class Server : public InternallyRefCounted<Server> {
 public:
  // Filter vtable.
  static const grpc_channel_filter kServerTopFilter;

  // Opaque type used for registered methods.
  struct RegisteredMethod;

  // An object to represent the most relevant characteristics of a
  // newly-allocated call object when using an AllocatingRequestMatcherBatch.
  struct BatchCallAllocation {
    void* tag;
    grpc_call** call;
    grpc_metadata_array* initial_metadata;
    grpc_call_details* details;
    grpc_completion_queue* cq;
  };

  // An object to represent the most relevant characteristics of a
  // newly-allocated call object when using an
  // AllocatingRequestMatcherRegistered.
  struct RegisteredCallAllocation {
    void* tag;
    grpc_call** call;
    grpc_metadata_array* initial_metadata;
    gpr_timespec* deadline;
    grpc_byte_buffer** optional_payload;
    grpc_completion_queue* cq;
  };

  /// Interface for listeners.
  /// Implementations must override the Orphan() method, which should stop
  /// listening and initiate destruction of the listener.
  class ListenerInterface : public Orphanable {
   public:
    ~ListenerInterface() override = default;

    /// Starts listening. This listener may refer to the pollset object beyond
    /// this call, so it is a pointer rather than a reference.
    virtual void Start(Server* server,
                       const std::vector<grpc_pollset*>* pollsets) = 0;

    /// Returns the channelz node for the listen socket, or null if not
    /// supported.
    virtual channelz::ListenSocketNode* channelz_listen_socket_node() const = 0;

    /// Sets a closure to be invoked by the listener when its destruction
    /// is complete.
    virtual void SetOnDestroyDone(grpc_closure* on_destroy_done) = 0;
  };

  explicit Server(const grpc_channel_args* args);
  ~Server() override;

  void Orphan() ABSL_LOCKS_EXCLUDED(mu_global_) override;

  const grpc_channel_args* channel_args() const { return channel_args_; }
  grpc_resource_user* default_resource_user() const {
    return default_resource_user_;
  }
  channelz::ServerNode* channelz_node() const { return channelz_node_.get(); }

  // Do not call this before Start(). Returns the pollsets. The
  // vector itself is immutable, but the pollsets inside are mutable. The
  // result is valid for the lifetime of the server.
  const std::vector<grpc_pollset*>& pollsets() const { return pollsets_; }

  grpc_server_config_fetcher* config_fetcher() const {
    return config_fetcher_.get();
  }

  void set_config_fetcher(
      std::unique_ptr<grpc_server_config_fetcher> config_fetcher) {
    config_fetcher_ = std::move(config_fetcher);
  }

  bool HasOpenConnections() ABSL_LOCKS_EXCLUDED(mu_global_);

  // Adds a listener to the server.  When the server starts, it will call
  // the listener's Start() method, and when it shuts down, it will orphan
  // the listener.
  void AddListener(OrphanablePtr<ListenerInterface> listener);

  // Starts listening for connections.
  void Start() ABSL_LOCKS_EXCLUDED(mu_global_);

  // Sets up a transport.  Creates a channel stack and binds the transport to
  // the server.  Called from the listener when a new connection is accepted.
  grpc_error_handle SetupTransport(
      grpc_transport* transport, grpc_pollset* accepting_pollset,
      const grpc_channel_args* args,
      const RefCountedPtr<channelz::SocketNode>& socket_node,
      grpc_resource_user* resource_user = nullptr);

  void RegisterCompletionQueue(grpc_completion_queue* cq);

  // Functions to specify that a specific registered method or the unregistered
  // collection should use a specific allocator for request matching.
  void SetRegisteredMethodAllocator(
      grpc_completion_queue* cq, void* method_tag,
      std::function<RegisteredCallAllocation()> allocator);
  void SetBatchMethodAllocator(grpc_completion_queue* cq,
                               std::function<BatchCallAllocation()> allocator);

  RegisteredMethod* RegisterMethod(
      const char* method, const char* host,
      grpc_server_register_method_payload_handling payload_handling,
      uint32_t flags);

  grpc_call_error RequestCall(grpc_call** call, grpc_call_details* details,
                              grpc_metadata_array* request_metadata,
                              grpc_completion_queue* cq_bound_to_call,
                              grpc_completion_queue* cq_for_notification,
                              void* tag);

  grpc_call_error RequestRegisteredCall(
      RegisteredMethod* rm, grpc_call** call, gpr_timespec* deadline,
      grpc_metadata_array* request_metadata,
      grpc_byte_buffer** optional_payload,
      grpc_completion_queue* cq_bound_to_call,
      grpc_completion_queue* cq_for_notification, void* tag_new);

  void ShutdownAndNotify(grpc_completion_queue* cq, void* tag)
      ABSL_LOCKS_EXCLUDED(mu_global_, mu_call_);

  void CancelAllCalls() ABSL_LOCKS_EXCLUDED(mu_global_);

 private:
  struct RequestedCall;

  struct ChannelRegisteredMethod {
    RegisteredMethod* server_registered_method = nullptr;
    uint32_t flags;
    bool has_host;
    ExternallyManagedSlice method;
    ExternallyManagedSlice host;
  };

  class RequestMatcherInterface;
  class RealRequestMatcher;
  class AllocatingRequestMatcherBase;
  class AllocatingRequestMatcherBatch;
  class AllocatingRequestMatcherRegistered;

  class ChannelData {
   public:
    ChannelData() = default;
    ~ChannelData();

    void InitTransport(RefCountedPtr<Server> server, grpc_channel* channel,
                       size_t cq_idx, grpc_transport* transport,
                       intptr_t channelz_socket_uuid);

    RefCountedPtr<Server> server() const { return server_; }
    grpc_channel* channel() const { return channel_; }
    size_t cq_idx() const { return cq_idx_; }

    ChannelRegisteredMethod* GetRegisteredMethod(const grpc_slice& host,
                                                 const grpc_slice& path,
                                                 bool is_idempotent);

    // Filter vtable functions.
    static grpc_error_handle InitChannelElement(
        grpc_channel_element* elem, grpc_channel_element_args* args);
    static void DestroyChannelElement(grpc_channel_element* elem);

   private:
    class ConnectivityWatcher;

    static void AcceptStream(void* arg, grpc_transport* /*transport*/,
                             const void* transport_server_data);

    void Destroy() ABSL_EXCLUSIVE_LOCKS_REQUIRED(server_->mu_global_);

    static void FinishDestroy(void* arg, grpc_error_handle error);

    RefCountedPtr<Server> server_;
    grpc_channel* channel_;
    // The index into Server::cqs_ of the CQ used as a starting point for
    // where to publish new incoming calls.
    size_t cq_idx_;
    absl::optional<std::list<ChannelData*>::iterator> list_position_;
    // A hash-table of the methods and hosts of the registered methods.
    // TODO(vjpai): Convert this to an STL map type as opposed to a direct
    // bucket implementation. (Consider performance impact, hash function to
    // use, etc.)
    std::unique_ptr<std::vector<ChannelRegisteredMethod>> registered_methods_;
    uint32_t registered_method_max_probes_;
    grpc_closure finish_destroy_channel_closure_;
    intptr_t channelz_socket_uuid_;
  };

  class CallData {
   public:
    enum class CallState {
      NOT_STARTED,  // Waiting for metadata.
      PENDING,      // Initial metadata read, not flow controlled in yet.
      ACTIVATED,    // Flow controlled in, on completion queue.
      ZOMBIED,      // Cancelled before being queued.
    };

    CallData(grpc_call_element* elem, const grpc_call_element_args& args,
             RefCountedPtr<Server> server);
    ~CallData();

    // Starts the recv_initial_metadata batch on the call.
    // Invoked from ChannelData::AcceptStream().
    void Start(grpc_call_element* elem);

    void SetState(CallState state);

    // Attempts to move from PENDING to ACTIVATED state.  Returns true
    // on success.
    bool MaybeActivate();

    // Publishes an incoming call to the application after it has been
    // matched.
    void Publish(size_t cq_idx, RequestedCall* rc);

    void KillZombie();

    void FailCallCreation();

    // Filter vtable functions.
    static grpc_error_handle InitCallElement(
        grpc_call_element* elem, const grpc_call_element_args* args);
    static void DestroyCallElement(grpc_call_element* elem,
                                   const grpc_call_final_info* /*final_info*/,
                                   grpc_closure* /*ignored*/);
    static void StartTransportStreamOpBatch(
        grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

   private:
    // Helper functions for handling calls at the top of the call stack.
    static void RecvInitialMetadataBatchComplete(void* arg,
                                                 grpc_error_handle error);
    void StartNewRpc(grpc_call_element* elem);
    static void PublishNewRpc(void* arg, grpc_error_handle error);

    // Functions used inside the call stack.
    void StartTransportStreamOpBatchImpl(grpc_call_element* elem,
                                         grpc_transport_stream_op_batch* batch);
    static void RecvInitialMetadataReady(void* arg, grpc_error_handle error);
    static void RecvTrailingMetadataReady(void* arg, grpc_error_handle error);

    RefCountedPtr<Server> server_;

    grpc_call* call_;

    Atomic<CallState> state_{CallState::NOT_STARTED};

    absl::optional<grpc_slice> path_;
    absl::optional<grpc_slice> host_;
    grpc_millis deadline_ = GRPC_MILLIS_INF_FUTURE;

    grpc_completion_queue* cq_new_ = nullptr;

    RequestMatcherInterface* matcher_ = nullptr;
    grpc_byte_buffer* payload_ = nullptr;

    grpc_closure kill_zombie_closure_;

    grpc_metadata_array initial_metadata_ =
        grpc_metadata_array();  // Zero-initialize the C struct.
    grpc_closure recv_initial_metadata_batch_complete_;

    grpc_metadata_batch* recv_initial_metadata_ = nullptr;
    uint32_t recv_initial_metadata_flags_ = 0;
    grpc_closure recv_initial_metadata_ready_;
    grpc_closure* original_recv_initial_metadata_ready_;
    grpc_error_handle recv_initial_metadata_error_ = GRPC_ERROR_NONE;

    bool seen_recv_trailing_metadata_ready_ = false;
    grpc_closure recv_trailing_metadata_ready_;
    grpc_closure* original_recv_trailing_metadata_ready_;
    grpc_error_handle recv_trailing_metadata_error_ = GRPC_ERROR_NONE;

    grpc_closure publish_;

    CallCombiner* call_combiner_;
  };

  struct Listener {
    explicit Listener(OrphanablePtr<ListenerInterface> l)
        : listener(std::move(l)) {}
    OrphanablePtr<ListenerInterface> listener;
    grpc_closure destroy_done;
  };

  struct ShutdownTag {
    ShutdownTag(void* tag_arg, grpc_completion_queue* cq_arg)
        : tag(tag_arg), cq(cq_arg) {}
    void* const tag;
    grpc_completion_queue* const cq;
    grpc_cq_completion completion;
  };

  static void ListenerDestroyDone(void* arg, grpc_error_handle error);

  static void DoneShutdownEvent(void* server,
                                grpc_cq_completion* /*completion*/) {
    static_cast<Server*>(server)->Unref();
  }

  static void DoneRequestEvent(void* req, grpc_cq_completion* completion);

  void FailCall(size_t cq_idx, RequestedCall* rc, grpc_error_handle error);
  grpc_call_error QueueRequestedCall(size_t cq_idx, RequestedCall* rc);

  void MaybeFinishShutdown() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_global_)
      ABSL_LOCKS_EXCLUDED(mu_call_);

  void KillPendingWorkLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_call_);

  static grpc_call_error ValidateServerRequest(
      grpc_completion_queue* cq_for_notification, void* tag,
      grpc_byte_buffer** optional_payload, RegisteredMethod* rm);
  grpc_call_error ValidateServerRequestAndCq(
      size_t* cq_idx, grpc_completion_queue* cq_for_notification, void* tag,
      grpc_byte_buffer** optional_payload, RegisteredMethod* rm);

  std::vector<grpc_channel*> GetChannelsLocked() const;

  // Take a shutdown ref for a request (increment by 2) and return if shutdown
  // has already been called.
  bool ShutdownRefOnRequest() {
    int old_value = shutdown_refs_.FetchAdd(2, MemoryOrder::ACQ_REL);
    return (old_value & 1) != 0;
  }

  // Decrement the shutdown ref counter by either 1 (for shutdown call) or 2
  // (for in-flight request) and possibly call MaybeFinishShutdown if
  // appropriate.
  void ShutdownUnrefOnRequest() ABSL_LOCKS_EXCLUDED(mu_global_) {
    if (shutdown_refs_.FetchSub(2, MemoryOrder::ACQ_REL) == 2) {
      MutexLock lock(&mu_global_);
      MaybeFinishShutdown();
    }
  }
  void ShutdownUnrefOnShutdownCall() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_global_) {
    if (shutdown_refs_.FetchSub(1, MemoryOrder::ACQ_REL) == 1) {
      MaybeFinishShutdown();
    }
  }

  bool ShutdownCalled() const {
    return (shutdown_refs_.Load(MemoryOrder::ACQUIRE) & 1) == 0;
  }

  // Returns whether there are no more shutdown refs, which means that shutdown
  // has been called and all accepted requests have been published if using an
  // AllocatingRequestMatcher.
  bool ShutdownReady() const {
    return shutdown_refs_.Load(MemoryOrder::ACQUIRE) == 0;
  }

  grpc_channel_args* const channel_args_;
  grpc_resource_user* default_resource_user_ = nullptr;
  RefCountedPtr<channelz::ServerNode> channelz_node_;
  std::unique_ptr<grpc_server_config_fetcher> config_fetcher_;

  std::vector<grpc_completion_queue*> cqs_;
  std::vector<grpc_pollset*> pollsets_;
  bool started_ = false;

  // The two following mutexes control access to server-state.
  // mu_global_ controls access to non-call-related state (e.g., channel state).
  // mu_call_ controls access to call-related state (e.g., the call lists).
  //
  // If they are ever required to be nested, you must lock mu_global_
  // before mu_call_. This is currently used in shutdown processing
  // (ShutdownAndNotify() and MaybeFinishShutdown()).
  Mutex mu_global_;  // mutex for server and channel state
  Mutex mu_call_;    // mutex for call-specific state

  // startup synchronization: flag is protected by mu_global_, signals whether
  // we are doing the listener start routine or not.
  bool starting_ = false;
  CondVar starting_cv_;

  std::vector<std::unique_ptr<RegisteredMethod>> registered_methods_;

  // Request matcher for unregistered methods.
  std::unique_ptr<RequestMatcherInterface> unregistered_request_matcher_;

  // The shutdown refs counter tracks whether or not shutdown has been called
  // and whether there are any AllocatingRequestMatcher requests that have been
  // accepted but not yet started (+2 on each one). If shutdown has been called,
  // the lowest bit will be 0 (defaults to 1) and the counter will be even. The
  // server should not notify on shutdown until the counter is 0 (shutdown is
  // called and there are no requests that are accepted but not started).
  Atomic<int> shutdown_refs_{1};
  bool shutdown_published_ ABSL_GUARDED_BY(mu_global_) = false;
  std::vector<ShutdownTag> shutdown_tags_ ABSL_GUARDED_BY(mu_global_);

  std::list<ChannelData*> channels_;

  std::list<Listener> listeners_;
  size_t listeners_destroyed_ = 0;

  // The last time we printed a shutdown progress message.
  gpr_timespec last_shutdown_message_time_;
};

}  // namespace grpc_core

struct grpc_server {
  grpc_core::OrphanablePtr<grpc_core::Server> core_server;
};

// TODO(roth): Eventually, will need a way to modify configuration even after
// a connection is established (e.g., to change things like L7 rate
// limiting, RBAC, and fault injection configs).  One possible option
// would be to do something like ServiceConfig and ConfigSelector, but
// that might add unnecessary per-call overhead.  Need to consider other
// approaches here.
struct grpc_server_config_fetcher {
 public:
  class ConnectionManager : public grpc_core::RefCounted<ConnectionManager> {
   public:
    // Ownership of \a args is transfered.
    virtual absl::StatusOr<grpc_channel_args*> UpdateChannelArgsForConnection(
        grpc_channel_args* args, grpc_endpoint* tcp) = 0;
  };

  class WatcherInterface {
   public:
    virtual ~WatcherInterface() = default;
    // UpdateConnectionManager() is invoked by the config fetcher when a new
    // config is available. Implementations should update the connection manager
    // and start serving if not already serving.
    virtual void UpdateConnectionManager(
        grpc_core::RefCountedPtr<ConnectionManager> manager) = 0;
    // Implementations should stop serving when this is called. Serving should
    // only resume when UpdateConfig() is invoked.
    virtual void StopServing() = 0;
  };

  virtual ~grpc_server_config_fetcher() = default;

  // Ownership of \a args is transferred.
  virtual void StartWatch(std::string listening_address,
                          grpc_channel_args* args,
                          std::unique_ptr<WatcherInterface> watcher) = 0;
  virtual void CancelWatch(WatcherInterface* watcher) = 0;
  virtual grpc_pollset_set* interested_parties() = 0;
};

#endif /* GRPC_CORE_LIB_SURFACE_SERVER_H */
