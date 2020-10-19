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

#include "absl/types/optional.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/atomic.h"
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
    grpc_experimental_completion_queue_functor* tag;
    grpc_call** call;
    grpc_metadata_array* initial_metadata;
    grpc_call_details* details;
  };

  // An object to represent the most relevant characteristics of a
  // newly-allocated call object when using an
  // AllocatingRequestMatcherRegistered.
  struct RegisteredCallAllocation {
    grpc_experimental_completion_queue_functor* tag;
    grpc_call** call;
    grpc_metadata_array* initial_metadata;
    gpr_timespec* deadline;
    grpc_byte_buffer** optional_payload;
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

  void Orphan() override;

  const grpc_channel_args* channel_args() const { return channel_args_; }
  grpc_resource_user* default_resource_user() const {
    return default_resource_user_;
  }
  channelz::ServerNode* channelz_node() const { return channelz_node_.get(); }

  // Do not call this before Start(). Returns the pollsets. The
  // vector itself is immutable, but the pollsets inside are mutable. The
  // result is valid for the lifetime of the server.
  const std::vector<grpc_pollset*>& pollsets() const { return pollsets_; }

  bool HasOpenConnections();

  // Adds a listener to the server.  When the server starts, it will call
  // the listener's Start() method, and when it shuts down, it will orphan
  // the listener.
  void AddListener(OrphanablePtr<ListenerInterface> listener);

  // Starts listening for connections.
  void Start();

  // Sets up a transport.  Creates a channel stack and binds the transport to
  // the server.  Called from the listener when a new connection is accepted.
  void SetupTransport(grpc_transport* transport,
                      grpc_pollset* accepting_pollset,
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

  void ShutdownAndNotify(grpc_completion_queue* cq, void* tag);

  void CancelAllCalls();

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
    static grpc_error* InitChannelElement(grpc_channel_element* elem,
                                          grpc_channel_element_args* args);
    static void DestroyChannelElement(grpc_channel_element* elem);

   private:
    class ConnectivityWatcher;

    static void AcceptStream(void* arg, grpc_transport* /*transport*/,
                             const void* transport_server_data);

    void Destroy();

    static void FinishDestroy(void* arg, grpc_error* error);

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
    static grpc_error* InitCallElement(grpc_call_element* elem,
                                       const grpc_call_element_args* args);
    static void DestroyCallElement(grpc_call_element* elem,
                                   const grpc_call_final_info* /*final_info*/,
                                   grpc_closure* /*ignored*/);
    static void StartTransportStreamOpBatch(
        grpc_call_element* elem, grpc_transport_stream_op_batch* batch);

   private:
    // Helper functions for handling calls at the top of the call stack.
    static void RecvInitialMetadataBatchComplete(void* arg, grpc_error* error);
    void StartNewRpc(grpc_call_element* elem);
    static void PublishNewRpc(void* arg, grpc_error* error);

    // Functions used inside the call stack.
    void StartTransportStreamOpBatchImpl(grpc_call_element* elem,
                                         grpc_transport_stream_op_batch* batch);
    static void RecvInitialMetadataReady(void* arg, grpc_error* error);
    static void RecvTrailingMetadataReady(void* arg, grpc_error* error);

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
    grpc_error* recv_initial_metadata_error_ = GRPC_ERROR_NONE;

    bool seen_recv_trailing_metadata_ready_ = false;
    grpc_closure recv_trailing_metadata_ready_;
    grpc_closure* original_recv_trailing_metadata_ready_;
    grpc_error* recv_trailing_metadata_error_ = GRPC_ERROR_NONE;

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

  static void ListenerDestroyDone(void* arg, grpc_error* error);

  static void DoneShutdownEvent(void* server,
                                grpc_cq_completion* /*completion*/) {
    static_cast<Server*>(server)->Unref();
  }

  static void DoneRequestEvent(void* req, grpc_cq_completion* completion);

  void FailCall(size_t cq_idx, RequestedCall* rc, grpc_error* error);
  grpc_call_error QueueRequestedCall(size_t cq_idx, RequestedCall* rc);

  void MaybeFinishShutdown();

  void KillPendingWorkLocked(grpc_error* error);

  static grpc_call_error ValidateServerRequest(
      grpc_completion_queue* cq_for_notification, void* tag,
      grpc_byte_buffer** optional_payload, RegisteredMethod* rm);
  grpc_call_error ValidateServerRequestAndCq(
      size_t* cq_idx, grpc_completion_queue* cq_for_notification, void* tag,
      grpc_byte_buffer** optional_payload, RegisteredMethod* rm);

  std::vector<grpc_channel*> GetChannelsLocked() const;

  grpc_channel_args* const channel_args_;
  grpc_resource_user* default_resource_user_ = nullptr;
  RefCountedPtr<channelz::ServerNode> channelz_node_;

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

  std::atomic_bool shutdown_flag_{false};
  bool shutdown_published_ = false;
  std::vector<ShutdownTag> shutdown_tags_;

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

#endif /* GRPC_CORE_LIB_SURFACE_SERVER_H */
