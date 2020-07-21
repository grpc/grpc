/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/server.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <list>
#include <utility>
#include <vector>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/spinlock.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/init.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/lib/transport/static_metadata.h"

namespace grpc_core {

TraceFlag grpc_server_channel_trace(false, "server_channel");

namespace {

void server_on_recv_initial_metadata(void* ptr, grpc_error* error);
void server_recv_trailing_metadata_ready(void* user_data, grpc_error* error);

struct Listener {
  explicit Listener(OrphanablePtr<ServerListenerInterface> l)
      : listener(std::move(l)) {}

  OrphanablePtr<ServerListenerInterface> listener;
  grpc_closure destroy_done;
};

enum class RequestedCallType { BATCH_CALL, REGISTERED_CALL };

struct registered_method;

struct requested_call {
  requested_call(void* tag_arg, grpc_completion_queue* call_cq,
                 grpc_call** call_arg, grpc_metadata_array* initial_md,
                 grpc_call_details* details)
      : type(RequestedCallType::BATCH_CALL),
        tag(tag_arg),
        cq_bound_to_call(call_cq),
        call(call_arg),
        initial_metadata(initial_md) {
    details->reserved = nullptr;
    data.batch.details = details;
  }

  requested_call(void* tag_arg, grpc_completion_queue* call_cq,
                 grpc_call** call_arg, grpc_metadata_array* initial_md,
                 registered_method* rm, gpr_timespec* deadline,
                 grpc_byte_buffer** optional_payload)
      : type(RequestedCallType::REGISTERED_CALL),
        tag(tag_arg),
        cq_bound_to_call(call_cq),
        call(call_arg),
        initial_metadata(initial_md) {
    data.registered.method = rm;
    data.registered.deadline = deadline;
    data.registered.optional_payload = optional_payload;
  }

  MultiProducerSingleConsumerQueue::Node mpscq_node;
  const RequestedCallType type;
  void* const tag;
  grpc_completion_queue* const cq_bound_to_call;
  grpc_call** const call;
  grpc_cq_completion completion;
  grpc_metadata_array* const initial_metadata;
  union {
    struct {
      grpc_call_details* details;
    } batch;
    struct {
      registered_method* method;
      gpr_timespec* deadline;
      grpc_byte_buffer** optional_payload;
    } registered;
  } data;
};

struct channel_registered_method {
  registered_method* server_registered_method = nullptr;
  uint32_t flags;
  bool has_host;
  ExternallyManagedSlice method;
  ExternallyManagedSlice host;
};

struct channel_data {
  channel_data() = default;
  ~channel_data();

  grpc_server* server = nullptr;
  grpc_channel* channel;
  size_t cq_idx;
  absl::optional<std::list<channel_data*>::iterator> list_position;

  // registered_methods is a hash-table of the methods and hosts of the
  // registered methods.
  // TODO(vjpai): Convert this to an STL map type as opposed to a direct bucket
  // implementation. (Consider performance impact, hash function to use, etc.)
  std::unique_ptr<std::vector<channel_registered_method>> registered_methods;
  uint32_t registered_method_max_probes;

  grpc_closure finish_destroy_channel_closure;
  intptr_t channelz_socket_uuid;
};

struct shutdown_tag {
  shutdown_tag(void* tag_arg, grpc_completion_queue* cq_arg)
      : tag(tag_arg), cq(cq_arg) {}

  void* const tag;
  grpc_completion_queue* const cq;
  grpc_cq_completion completion;
};

enum class CallState {
  /* waiting for metadata */
  NOT_STARTED,
  /* initial metadata read, not flow controlled in yet */
  PENDING,
  /* flow controlled in, on completion queue */
  ACTIVATED,
  /* cancelled before being queued */
  ZOMBIED
};

struct call_data;

grpc_call_error ValidateServerRequest(
    grpc_completion_queue* cq_for_notification, void* tag,
    grpc_byte_buffer** optional_payload, registered_method* rm);

// RPCs that come in from the transport must be matched against RPC requests
// from the application. An incoming request from the application can be matched
// to an RPC that has already arrived or can be queued up for later use.
// Likewise, an RPC coming in from the transport can either be matched to a
// request that already arrived from the application or can be queued up for
// later use (marked pending). If there is a match, the request's tag is posted
// on the request's notification CQ.
//
// RequestMatcherInterface is the base class to provide this functionality.
class RequestMatcherInterface {
 public:
  virtual ~RequestMatcherInterface() {}

  // Unref the calls associated with any incoming RPCs in the pending queue (not
  // yet matched to an application-requested RPC).
  virtual void ZombifyPending() = 0;

  // Mark all application-requested RPCs failed if they have not been matched to
  // an incoming RPC. The error parameter indicates why the RPCs are being
  // failed (always server shutdown in all current implementations).
  virtual void KillRequests(grpc_error* error) = 0;

  // How many request queues are supported by this matcher. This is an abstract
  // concept that essentially maps to gRPC completion queues.
  virtual size_t request_queue_count() const = 0;

  // This function is invoked when the application requests a new RPC whose
  // information is in the call parameter. The request_queue_index marks the
  // queue onto which to place this RPC, and is typically associated with a gRPC
  // CQ. If there are pending RPCs waiting to be matched, publish one (match it
  // and notify the CQ).
  virtual void RequestCallWithPossiblePublish(size_t request_queue_index,
                                              requested_call* call) = 0;

  // This function is invoked on an incoming RPC, represented by the calld
  // object. The RequestMatcher will try to match it against an
  // application-requested RPC if possible or will place it in the pending queue
  // otherwise. To enable some measure of fairness between server CQs, the match
  // is done starting at the start_request_queue_index parameter in a cyclic
  // order rather than always starting at 0.
  virtual void MatchOrQueue(size_t start_request_queue_index,
                            call_data* calld) = 0;

  // Returns the server associated with this request matcher
  virtual grpc_server* server() const = 0;
};

struct call_data {
  call_data(grpc_call_element* elem, const grpc_call_element_args& args)
      : call(grpc_call_from_top_element(elem)),
        call_combiner(args.call_combiner) {
    GRPC_CLOSURE_INIT(&on_recv_initial_metadata,
                      server_on_recv_initial_metadata, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready,
                      server_recv_trailing_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
  }
  ~call_data() {
    GPR_ASSERT(state.Load(grpc_core::MemoryOrder::RELAXED) !=
               CallState::PENDING);
    GRPC_ERROR_UNREF(recv_initial_metadata_error);
    if (host_set) {
      grpc_slice_unref_internal(host);
    }
    if (path_set) {
      grpc_slice_unref_internal(path);
    }
    grpc_metadata_array_destroy(&initial_metadata);
    grpc_byte_buffer_destroy(payload);
  }

  grpc_call* call;

  Atomic<CallState> state{CallState::NOT_STARTED};

  bool path_set = false;
  bool host_set = false;
  grpc_slice path;
  grpc_slice host;
  grpc_millis deadline = GRPC_MILLIS_INF_FUTURE;

  grpc_completion_queue* cq_new = nullptr;

  grpc_metadata_batch* recv_initial_metadata = nullptr;
  uint32_t recv_initial_metadata_flags = 0;
  grpc_metadata_array initial_metadata =
      grpc_metadata_array();  // Zero-initialize the C struct.

  RequestMatcherInterface* matcher = nullptr;
  grpc_byte_buffer* payload = nullptr;

  grpc_closure got_initial_metadata;
  grpc_closure on_recv_initial_metadata;
  grpc_closure kill_zombie_closure;
  grpc_closure* on_done_recv_initial_metadata;
  grpc_closure recv_trailing_metadata_ready;
  grpc_error* recv_initial_metadata_error = GRPC_ERROR_NONE;
  grpc_closure* original_recv_trailing_metadata_ready;
  grpc_error* recv_trailing_metadata_error = GRPC_ERROR_NONE;
  bool seen_recv_trailing_metadata_ready = false;

  grpc_closure publish;

  CallCombiner* call_combiner;
};

struct registered_method {
  registered_method(
      const char* method_arg, const char* host_arg,
      grpc_server_register_method_payload_handling payload_handling_arg,
      uint32_t flags_arg)
      : method(method_arg == nullptr ? "" : method_arg),
        host(host_arg == nullptr ? "" : host_arg),
        payload_handling(payload_handling_arg),
        flags(flags_arg) {}

  ~registered_method() = default;

  const std::string method;
  const std::string host;
  const grpc_server_register_method_payload_handling payload_handling;
  const uint32_t flags;
  /* one request matcher per method */
  std::unique_ptr<RequestMatcherInterface> matcher;
};

}  // namespace
}  // namespace grpc_core

struct grpc_server {
  explicit grpc_server(const grpc_channel_args* args)
      : channel_args(grpc_channel_args_copy(args)) {
    if (grpc_channel_args_find_bool(args, GRPC_ARG_ENABLE_CHANNELZ,
                                    GRPC_ENABLE_CHANNELZ_DEFAULT)) {
      size_t channel_tracer_max_memory = grpc_channel_args_find_integer(
          args, GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE,
          {GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT, 0, INT_MAX});
      channelz_server =
          grpc_core::MakeRefCounted<grpc_core::channelz::ServerNode>(
              this, channel_tracer_max_memory);
      channelz_server->AddTraceEvent(
          grpc_core::channelz::ChannelTrace::Severity::Info,
          grpc_slice_from_static_string("Server created"));
    }

    if (args != nullptr) {
      grpc_resource_quota* resource_quota =
          grpc_resource_quota_from_channel_args(args, false /* create */);
      if (resource_quota != nullptr) {
        default_resource_user =
            grpc_resource_user_create(resource_quota, "default");
      }
    }
  }

  ~grpc_server() {
    grpc_channel_args_destroy(channel_args);
    for (size_t i = 0; i < cqs.size(); i++) {
      GRPC_CQ_INTERNAL_UNREF(cqs[i], "server");
    }
  }

  grpc_channel_args* const channel_args;

  grpc_resource_user* default_resource_user = nullptr;

  std::vector<grpc_completion_queue*> cqs;
  std::vector<grpc_pollset*> pollsets;
  bool started = false;

  /* The two following mutexes control access to server-state
     mu_global controls access to non-call-related state (e.g., channel state)
     mu_call controls access to call-related state (e.g., the call lists)

     If they are ever required to be nested, you must lock mu_global
     before mu_call. This is currently used in shutdown processing
     (grpc_server_shutdown_and_notify and maybe_finish_shutdown) */
  grpc_core::Mutex mu_global;  // mutex for server and channel state
  grpc_core::Mutex mu_call;    // mutex for call-specific state

  /* startup synchronization: flag is protected by mu_global, signals whether
     we are doing the listener start routine or not */
  bool starting = false;
  grpc_core::CondVar starting_cv;

  std::vector<std::unique_ptr<grpc_core::registered_method>> registered_methods;

  // one request matcher for unregistered methods
  std::unique_ptr<grpc_core::RequestMatcherInterface>
      unregistered_request_matcher;

  std::atomic_bool shutdown_flag{false};
  bool shutdown_published = false;
  std::vector<grpc_core::shutdown_tag> shutdown_tags;

  std::list<grpc_core::channel_data*> channels;

  std::list<grpc_core::Listener> listeners;
  size_t listeners_destroyed = 0;
  grpc_core::RefCount internal_refcount;

  /** when did we print the last shutdown progress message */
  gpr_timespec last_shutdown_message_time;

  grpc_core::RefCountedPtr<grpc_core::channelz::ServerNode> channelz_server;
};

// Non-API functions of the server that are only for gRPC core internal use.
// TODO(markdroth): Make these class member functions
void grpc_server_add_listener(
    grpc_server* server,
    grpc_core::OrphanablePtr<grpc_core::ServerListenerInterface> listener) {
  grpc_core::channelz::ListenSocketNode* listen_socket_node =
      listener->channelz_listen_socket_node();
  if (listen_socket_node != nullptr && server->channelz_server != nullptr) {
    server->channelz_server->AddChildListenSocket(listen_socket_node->Ref());
  }
  server->listeners.emplace_back(std::move(listener));
}

const grpc_channel_args* grpc_server_get_channel_args(grpc_server* server) {
  return server->channel_args;
}

grpc_resource_user* grpc_server_get_default_resource_user(grpc_server* server) {
  return server->default_resource_user;
}

bool grpc_server_has_open_connections(grpc_server* server) {
  grpc_core::MutexLock lock(&server->mu_global);
  return !server->channels.empty();
}

grpc_core::channelz::ServerNode* grpc_server_get_channelz_node(
    grpc_server* server) {
  if (server == nullptr) {
    return nullptr;
  }
  return server->channelz_server.get();
}

namespace grpc_core {
namespace {

void publish_call(grpc_server* server, call_data* calld, size_t cq_idx,
                  requested_call* rc);
void fail_call(grpc_server* server, size_t cq_idx, requested_call* rc,
               grpc_error* error);
/* Before calling maybe_finish_shutdown, we must hold mu_global and not
   hold mu_call */
void maybe_finish_shutdown(grpc_server* server);

void kill_zombie(void* elem, grpc_error* /*error*/) {
  grpc_call_unref(
      grpc_call_from_top_element(static_cast<grpc_call_element*>(elem)));
}

// Validate a requested RPC for a server CQ and bind it to that CQ
grpc_call_error ValidateServerRequest(
    grpc_completion_queue* cq_for_notification, void* tag,
    grpc_byte_buffer** optional_payload, registered_method* rm) {
  if ((rm == nullptr && optional_payload != nullptr) ||
      ((rm != nullptr) && ((optional_payload == nullptr) !=
                           (rm->payload_handling == GRPC_SRM_PAYLOAD_NONE)))) {
    return GRPC_CALL_ERROR_PAYLOAD_TYPE_MISMATCH;
  }
  if (grpc_cq_begin_op(cq_for_notification, tag) == false) {
    return GRPC_CALL_ERROR_COMPLETION_QUEUE_SHUTDOWN;
  }
  return GRPC_CALL_OK;
}

// Validate that a requested RPC has a valid server CQ and is valid, and bind it
grpc_call_error ValidateServerRequestAndCq(
    size_t* cq_idx, grpc_server* server,
    grpc_completion_queue* cq_for_notification, void* tag,
    grpc_byte_buffer** optional_payload, registered_method* rm) {
  size_t idx;
  for (idx = 0; idx < server->cqs.size(); idx++) {
    if (server->cqs[idx] == cq_for_notification) {
      break;
    }
  }
  if (idx == server->cqs.size()) {
    return GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE;
  }
  grpc_call_error error =
      ValidateServerRequest(cq_for_notification, tag, optional_payload, rm);
  if (error != GRPC_CALL_OK) {
    return error;
  }

  *cq_idx = idx;
  return GRPC_CALL_OK;
}
/*
 * channel broadcaster
 */

struct shutdown_cleanup_args {
  grpc_closure closure;
  grpc_slice slice;
};

void shutdown_cleanup(void* arg, grpc_error* /*error*/) {
  shutdown_cleanup_args* a = static_cast<shutdown_cleanup_args*>(arg);
  grpc_slice_unref_internal(a->slice);
  delete a;
}

void send_shutdown(grpc_channel* channel, bool send_goaway,
                   grpc_error* send_disconnect) {
  shutdown_cleanup_args* sc = new shutdown_cleanup_args;
  GRPC_CLOSURE_INIT(&sc->closure, shutdown_cleanup, sc,
                    grpc_schedule_on_exec_ctx);
  grpc_transport_op* op = grpc_make_transport_op(&sc->closure);
  grpc_channel_element* elem;

  op->goaway_error =
      send_goaway ? grpc_error_set_int(
                        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Server shutdown"),
                        GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_OK)
                  : GRPC_ERROR_NONE;
  op->set_accept_stream = true;
  sc->slice = grpc_slice_from_copied_string("Server shutdown");
  op->disconnect_with_error = send_disconnect;

  elem = grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  elem->filter->start_transport_op(elem, op);
}

class ChannelBroadcaster {
 public:
  // This can have an empty constructor and destructor since we want to control
  // when the actual setup and shutdown broadcast take place

  // This function copies over the channels from the locked server
  void FillChannelsLocked(const grpc_server* s) {
    GPR_DEBUG_ASSERT(channels_.empty());
    channels_.reserve(s->channels.size());
    for (const channel_data* chand : s->channels) {
      channels_.push_back(chand->channel);
      GRPC_CHANNEL_INTERNAL_REF(chand->channel, "broadcast");
    }
  }

  // Broadcast a shutdown on each channel
  void BroadcastShutdown(bool send_goaway, grpc_error* force_disconnect) {
    for (grpc_channel* channel : channels_) {
      send_shutdown(channel, send_goaway, GRPC_ERROR_REF(force_disconnect));
      GRPC_CHANNEL_INTERNAL_UNREF(channel, "broadcast");
    }
    channels_.clear();  // just for safety against double broadcast
    GRPC_ERROR_UNREF(force_disconnect);
  }

 private:
  std::vector<grpc_channel*> channels_;
};

/*
 * request_matcher
 */

// The RealRequestMatcher is an implementation of RequestMatcherInterface that
// actually uses all the features of RequestMatcherInterface: expecting the
// application to explicitly request RPCs and then matching those to incoming
// RPCs, along with a slow path by which incoming RPCs are put on a locked
// pending list if they aren't able to be matched to an application request.
class RealRequestMatcher : public RequestMatcherInterface {
 public:
  explicit RealRequestMatcher(grpc_server* server)
      : server_(server), requests_per_cq_(server->cqs.size()) {}

  ~RealRequestMatcher() override {
    for (LockedMultiProducerSingleConsumerQueue& queue : requests_per_cq_) {
      GPR_ASSERT(queue.Pop() == nullptr);
    }
  }

  void ZombifyPending() override {
    for (call_data* calld : pending_) {
      calld->state.Store(CallState::ZOMBIED, grpc_core::MemoryOrder::RELAXED);
      GRPC_CLOSURE_INIT(
          &calld->kill_zombie_closure, kill_zombie,
          grpc_call_stack_element(grpc_call_get_call_stack(calld->call), 0),
          grpc_schedule_on_exec_ctx);
      ExecCtx::Run(DEBUG_LOCATION, &calld->kill_zombie_closure,
                   GRPC_ERROR_NONE);
    }
    pending_.clear();
  }

  void KillRequests(grpc_error* error) override {
    for (size_t i = 0; i < requests_per_cq_.size(); i++) {
      requested_call* rc;
      while ((rc = reinterpret_cast<requested_call*>(
                  requests_per_cq_[i].Pop())) != nullptr) {
        fail_call(server_, i, rc, GRPC_ERROR_REF(error));
      }
    }
    GRPC_ERROR_UNREF(error);
  }

  size_t request_queue_count() const override {
    return requests_per_cq_.size();
  }

  void RequestCallWithPossiblePublish(size_t request_queue_index,
                                      requested_call* call) override {
    if (requests_per_cq_[request_queue_index].Push(&call->mpscq_node)) {
      /* this was the first queued request: we need to lock and start
         matching calls */
      struct PendingCall {
        requested_call* rc = nullptr;
        call_data* calld;
      };
      auto pop_next_pending = [this, request_queue_index] {
        PendingCall pending;
        {
          MutexLock lock(&server_->mu_call);
          if (!pending_.empty()) {
            pending.rc = reinterpret_cast<requested_call*>(
                requests_per_cq_[request_queue_index].Pop());
            if (pending.rc != nullptr) {
              pending.calld = pending_.front();
              pending_.pop_front();
            }
          }
        }
        return pending;
      };
      while (true) {
        PendingCall next_pending = pop_next_pending();
        if (next_pending.rc == nullptr) break;
        CallState expect_pending = CallState::PENDING;
        if (!next_pending.calld->state.CompareExchangeStrong(
                &expect_pending, CallState::ACTIVATED,
                grpc_core::MemoryOrder::ACQ_REL,
                grpc_core::MemoryOrder::RELAXED)) {
          // Zombied Call
          GRPC_CLOSURE_INIT(
              &next_pending.calld->kill_zombie_closure, kill_zombie,
              grpc_call_stack_element(
                  grpc_call_get_call_stack(next_pending.calld->call), 0),
              grpc_schedule_on_exec_ctx);
          ExecCtx::Run(DEBUG_LOCATION, &next_pending.calld->kill_zombie_closure,
                       GRPC_ERROR_NONE);
        } else {
          publish_call(server_, next_pending.calld, request_queue_index,
                       next_pending.rc);
        }
      }
    }
  }

  void MatchOrQueue(size_t start_request_queue_index,
                    call_data* calld) override {
    for (size_t i = 0; i < requests_per_cq_.size(); i++) {
      size_t cq_idx = (start_request_queue_index + i) % requests_per_cq_.size();
      requested_call* rc =
          reinterpret_cast<requested_call*>(requests_per_cq_[cq_idx].TryPop());
      if (rc == nullptr) {
        continue;
      } else {
        GRPC_STATS_INC_SERVER_CQS_CHECKED(i);
        calld->state.Store(CallState::ACTIVATED,
                           grpc_core::MemoryOrder::RELAXED);
        publish_call(server_, calld, cq_idx, rc);
        return; /* early out */
      }
    }

    /* no cq to take the request found: queue it on the slow list */
    GRPC_STATS_INC_SERVER_SLOWPATH_REQUESTS_QUEUED();

    // We need to ensure that all the queues are empty.  We do this under
    // the server mu_call lock to ensure that if something is added to
    // an empty request queue, it will block until the call is actually
    // added to the pending list.
    requested_call* rc = nullptr;
    size_t cq_idx = 0;
    size_t loop_count;
    {
      MutexLock lock(&server_->mu_call);
      for (loop_count = 0; loop_count < requests_per_cq_.size(); loop_count++) {
        cq_idx =
            (start_request_queue_index + loop_count) % requests_per_cq_.size();
        rc = reinterpret_cast<requested_call*>(requests_per_cq_[cq_idx].Pop());
        if (rc != nullptr) {
          break;
        }
      }
      if (rc == nullptr) {
        calld->state.Store(CallState::PENDING, grpc_core::MemoryOrder::RELAXED);
        pending_.push_back(calld);
        return;
      }
    }
    GRPC_STATS_INC_SERVER_CQS_CHECKED(loop_count + requests_per_cq_.size());
    calld->state.Store(CallState::ACTIVATED, grpc_core::MemoryOrder::RELAXED);
    publish_call(server_, calld, cq_idx, rc);
  }

  grpc_server* server() const override { return server_; }

 private:
  grpc_server* const server_;
  std::list<call_data*> pending_;
  std::vector<LockedMultiProducerSingleConsumerQueue> requests_per_cq_;
};

// AllocatingRequestMatchers don't allow the application to request an RPC in
// advance or queue up any incoming RPC for later match. Instead, MatchOrQueue
// will call out to an allocation function passed in at the construction of the
// object. These request matchers are designed for the C++ callback API, so they
// only support 1 completion queue (passed in at the constructor).
class AllocatingRequestMatcherBase : public RequestMatcherInterface {
 public:
  AllocatingRequestMatcherBase(grpc_server* server, grpc_completion_queue* cq)
      : server_(server), cq_(cq) {
    size_t idx;
    for (idx = 0; idx < server->cqs.size(); idx++) {
      if (server->cqs[idx] == cq) {
        break;
      }
    }
    GPR_ASSERT(idx < server->cqs.size());
    cq_idx_ = idx;
  }

  void ZombifyPending() override {}

  void KillRequests(grpc_error* error) override { GRPC_ERROR_UNREF(error); }

  size_t request_queue_count() const override { return 0; }

  void RequestCallWithPossiblePublish(size_t /*request_queue_index*/,
                                      requested_call* /*call*/) final {
    GPR_ASSERT(false);
  }

  grpc_server* server() const override { return server_; }

  // Supply the completion queue related to this request matcher
  grpc_completion_queue* cq() const { return cq_; }

  // Supply the completion queue's index relative to the server.
  size_t cq_idx() const { return cq_idx_; }

 private:
  grpc_server* const server_;
  grpc_completion_queue* const cq_;
  size_t cq_idx_;
};

// An allocating request matcher for non-registered methods (used for generic
// API and unimplemented RPCs).
class AllocatingRequestMatcherBatch : public AllocatingRequestMatcherBase {
 public:
  AllocatingRequestMatcherBatch(
      grpc_server* server, grpc_completion_queue* cq,
      std::function<ServerBatchCallAllocation()> allocator)
      : AllocatingRequestMatcherBase(server, cq),
        allocator_(std::move(allocator)) {}
  void MatchOrQueue(size_t /*start_request_queue_index*/,
                    call_data* calld) override {
    ServerBatchCallAllocation call_info = allocator_();
    GPR_ASSERT(ValidateServerRequest(cq(), static_cast<void*>(call_info.tag),
                                     nullptr, nullptr) == GRPC_CALL_OK);
    requested_call* rc = new requested_call(
        static_cast<void*>(call_info.tag), cq(), call_info.call,
        call_info.initial_metadata, call_info.details);
    calld->state.Store(CallState::ACTIVATED, grpc_core::MemoryOrder::RELAXED);
    publish_call(server(), calld, cq_idx(), rc);
  }

 private:
  std::function<ServerBatchCallAllocation()> allocator_;
};

// An allocating request matcher for registered methods.
class AllocatingRequestMatcherRegistered : public AllocatingRequestMatcherBase {
 public:
  AllocatingRequestMatcherRegistered(
      grpc_server* server, grpc_completion_queue* cq, registered_method* rm,
      std::function<ServerRegisteredCallAllocation()> allocator)
      : AllocatingRequestMatcherBase(server, cq),
        registered_method_(rm),
        allocator_(std::move(allocator)) {}
  void MatchOrQueue(size_t /*start_request_queue_index*/,
                    call_data* calld) override {
    ServerRegisteredCallAllocation call_info = allocator_();
    GPR_ASSERT(ValidateServerRequest(cq(), static_cast<void*>(call_info.tag),
                                     call_info.optional_payload,
                                     registered_method_) == GRPC_CALL_OK);
    requested_call* rc = new requested_call(
        static_cast<void*>(call_info.tag), cq(), call_info.call,
        call_info.initial_metadata, registered_method_, call_info.deadline,
        call_info.optional_payload);
    calld->state.Store(CallState::ACTIVATED, grpc_core::MemoryOrder::RELAXED);
    publish_call(server(), calld, cq_idx(), rc);
  }

 private:
  registered_method* const registered_method_;
  std::function<ServerRegisteredCallAllocation()> allocator_;
};

/*
 * server proper
 */

void server_ref(grpc_server* server) { server->internal_refcount.Ref(); }

void server_unref(grpc_server* server) {
  if (GPR_UNLIKELY(server->internal_refcount.Unref())) {
    delete server;
  }
}

void finish_destroy_channel(void* cd, grpc_error* /*error*/) {
  channel_data* chand = static_cast<channel_data*>(cd);
  grpc_server* server = chand->server;
  GRPC_CHANNEL_INTERNAL_UNREF(chand->channel, "server");
  server_unref(server);
}

void destroy_channel(channel_data* chand) {
  if (!chand->list_position.has_value()) return;
  GPR_ASSERT(chand->server != nullptr);
  chand->server->channels.erase(*chand->list_position);
  chand->list_position.reset();
  server_ref(chand->server);
  maybe_finish_shutdown(chand->server);
  GRPC_CLOSURE_INIT(&chand->finish_destroy_channel_closure,
                    finish_destroy_channel, chand, grpc_schedule_on_exec_ctx);

  if (GRPC_TRACE_FLAG_ENABLED(grpc_server_channel_trace)) {
    gpr_log(GPR_INFO, "Disconnected client");
  }

  grpc_transport_op* op =
      grpc_make_transport_op(&chand->finish_destroy_channel_closure);
  op->set_accept_stream = true;
  grpc_channel_next_op(grpc_channel_stack_element(
                           grpc_channel_get_channel_stack(chand->channel), 0),
                       op);
}

void done_request_event(void* req, grpc_cq_completion* /*c*/) {
  delete static_cast<requested_call*>(req);
}

void publish_call(grpc_server* server, call_data* calld, size_t cq_idx,
                  requested_call* rc) {
  grpc_call_set_completion_queue(calld->call, rc->cq_bound_to_call);
  grpc_call* call = calld->call;
  *rc->call = call;
  calld->cq_new = server->cqs[cq_idx];
  GPR_SWAP(grpc_metadata_array, *rc->initial_metadata, calld->initial_metadata);
  switch (rc->type) {
    case RequestedCallType::BATCH_CALL:
      GPR_ASSERT(calld->host_set);
      GPR_ASSERT(calld->path_set);
      rc->data.batch.details->host = grpc_slice_ref_internal(calld->host);
      rc->data.batch.details->method = grpc_slice_ref_internal(calld->path);
      rc->data.batch.details->deadline =
          grpc_millis_to_timespec(calld->deadline, GPR_CLOCK_MONOTONIC);
      rc->data.batch.details->flags = calld->recv_initial_metadata_flags;
      break;
    case RequestedCallType::REGISTERED_CALL:
      *rc->data.registered.deadline =
          grpc_millis_to_timespec(calld->deadline, GPR_CLOCK_MONOTONIC);
      if (rc->data.registered.optional_payload) {
        *rc->data.registered.optional_payload = calld->payload;
        calld->payload = nullptr;
      }
      break;
    default:
      GPR_UNREACHABLE_CODE(return );
  }

  grpc_cq_end_op(calld->cq_new, rc->tag, GRPC_ERROR_NONE, done_request_event,
                 rc, &rc->completion, true);
}

void publish_new_rpc(void* arg, grpc_error* error) {
  grpc_call_element* call_elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(call_elem->call_data);
  channel_data* chand = static_cast<channel_data*>(call_elem->channel_data);
  RequestMatcherInterface* rm = calld->matcher;
  grpc_server* server = rm->server();

  if (error != GRPC_ERROR_NONE ||
      server->shutdown_flag.load(std::memory_order_acquire)) {
    calld->state.Store(CallState::ZOMBIED, grpc_core::MemoryOrder::RELAXED);
    GRPC_CLOSURE_INIT(
        &calld->kill_zombie_closure, kill_zombie,
        grpc_call_stack_element(grpc_call_get_call_stack(calld->call), 0),
        grpc_schedule_on_exec_ctx);
    ExecCtx::Run(DEBUG_LOCATION, &calld->kill_zombie_closure,
                 GRPC_ERROR_REF(error));
    return;
  }

  rm->MatchOrQueue(chand->cq_idx, calld);
}

void finish_start_new_rpc(
    grpc_server* server, grpc_call_element* elem, RequestMatcherInterface* rm,
    grpc_server_register_method_payload_handling payload_handling) {
  call_data* calld = static_cast<call_data*>(elem->call_data);

  if (server->shutdown_flag.load(std::memory_order_acquire)) {
    calld->state.Store(CallState::ZOMBIED, grpc_core::MemoryOrder::RELAXED);
    GRPC_CLOSURE_INIT(&calld->kill_zombie_closure, kill_zombie, elem,
                      grpc_schedule_on_exec_ctx);
    ExecCtx::Run(DEBUG_LOCATION, &calld->kill_zombie_closure, GRPC_ERROR_NONE);
    return;
  }

  calld->matcher = rm;

  switch (payload_handling) {
    case GRPC_SRM_PAYLOAD_NONE:
      publish_new_rpc(elem, GRPC_ERROR_NONE);
      break;
    case GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER: {
      grpc_op op;
      op.op = GRPC_OP_RECV_MESSAGE;
      op.flags = 0;
      op.reserved = nullptr;
      op.data.recv_message.recv_message = &calld->payload;
      GRPC_CLOSURE_INIT(&calld->publish, publish_new_rpc, elem,
                        grpc_schedule_on_exec_ctx);
      grpc_call_start_batch_and_execute(calld->call, &op, 1, &calld->publish);
      break;
    }
  }
}

void start_new_rpc(grpc_call_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_server* server = chand->server;
  uint32_t i;
  uint32_t hash;
  channel_registered_method* rm;

  if (chand->registered_methods && calld->path_set && calld->host_set) {
    /* TODO(ctiller): unify these two searches */
    /* check for an exact match with host */
    hash = GRPC_MDSTR_KV_HASH(grpc_slice_hash_internal(calld->host),
                              grpc_slice_hash_internal(calld->path));
    for (i = 0; i <= chand->registered_method_max_probes; i++) {
      rm = &(*chand->registered_methods)[(hash + i) %
                                         chand->registered_methods->size()];
      if (rm->server_registered_method == nullptr) break;
      if (!rm->has_host) continue;
      if (rm->host != calld->host) continue;
      if (rm->method != calld->path) continue;
      if ((rm->flags & GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST) &&
          0 == (calld->recv_initial_metadata_flags &
                GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST)) {
        continue;
      }
      finish_start_new_rpc(server, elem,
                           rm->server_registered_method->matcher.get(),
                           rm->server_registered_method->payload_handling);
      return;
    }
    /* check for a wildcard method definition (no host set) */
    hash = GRPC_MDSTR_KV_HASH(0, grpc_slice_hash_internal(calld->path));
    for (i = 0; i <= chand->registered_method_max_probes; i++) {
      rm = &(*chand->registered_methods)[(hash + i) %
                                         chand->registered_methods->size()];
      if (rm->server_registered_method == nullptr) break;
      if (rm->has_host) continue;
      if (rm->method != calld->path) continue;
      if ((rm->flags & GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST) &&
          0 == (calld->recv_initial_metadata_flags &
                GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST)) {
        continue;
      }
      finish_start_new_rpc(server, elem,
                           rm->server_registered_method->matcher.get(),
                           rm->server_registered_method->payload_handling);
      return;
    }
  }
  finish_start_new_rpc(server, elem, server->unregistered_request_matcher.get(),
                       GRPC_SRM_PAYLOAD_NONE);
}

void done_shutdown_event(void* server, grpc_cq_completion* /*completion*/) {
  server_unref(static_cast<grpc_server*>(server));
}

int num_channels(grpc_server* server) { return server->channels.size(); }

void kill_pending_work_locked(grpc_server* server, grpc_error* error) {
  if (server->started) {
    server->unregistered_request_matcher->KillRequests(GRPC_ERROR_REF(error));
    server->unregistered_request_matcher->ZombifyPending();
    for (std::unique_ptr<registered_method>& rm : server->registered_methods) {
      rm->matcher->KillRequests(GRPC_ERROR_REF(error));
      rm->matcher->ZombifyPending();
    }
  }
  GRPC_ERROR_UNREF(error);
}

void maybe_finish_shutdown(grpc_server* server) {
  size_t i;
  if (!server->shutdown_flag.load(std::memory_order_acquire) ||
      server->shutdown_published) {
    return;
  }

  {
    MutexLock lock(&server->mu_call);
    kill_pending_work_locked(
        server, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Server Shutdown"));
  }

  if (!server->channels.empty() ||
      server->listeners_destroyed < server->listeners.size()) {
    if (gpr_time_cmp(gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME),
                                  server->last_shutdown_message_time),
                     gpr_time_from_seconds(1, GPR_TIMESPAN)) >= 0) {
      server->last_shutdown_message_time = gpr_now(GPR_CLOCK_REALTIME);
      gpr_log(GPR_DEBUG,
              "Waiting for %d channels and %" PRIuPTR "/%" PRIuPTR
              " listeners to be destroyed before shutting down server",
              num_channels(server),
              server->listeners.size() - server->listeners_destroyed,
              server->listeners.size());
    }
    return;
  }
  server->shutdown_published = 1;
  for (i = 0; i < server->shutdown_tags.size(); i++) {
    server_ref(server);
    grpc_cq_end_op(server->shutdown_tags[i].cq, server->shutdown_tags[i].tag,
                   GRPC_ERROR_NONE, done_shutdown_event, server,
                   &server->shutdown_tags[i].completion);
  }
}

void server_on_recv_initial_metadata(void* ptr, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(ptr);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_millis op_deadline;

  if (error == GRPC_ERROR_NONE) {
    GPR_DEBUG_ASSERT(calld->recv_initial_metadata->idx.named.path != nullptr);
    GPR_DEBUG_ASSERT(calld->recv_initial_metadata->idx.named.authority !=
                     nullptr);
    calld->path = grpc_slice_ref_internal(
        GRPC_MDVALUE(calld->recv_initial_metadata->idx.named.path->md));
    calld->host = grpc_slice_ref_internal(
        GRPC_MDVALUE(calld->recv_initial_metadata->idx.named.authority->md));
    calld->path_set = true;
    calld->host_set = true;
    grpc_metadata_batch_remove(calld->recv_initial_metadata, GRPC_BATCH_PATH);
    grpc_metadata_batch_remove(calld->recv_initial_metadata,
                               GRPC_BATCH_AUTHORITY);
  } else {
    GRPC_ERROR_REF(error);
  }
  op_deadline = calld->recv_initial_metadata->deadline;
  if (op_deadline != GRPC_MILLIS_INF_FUTURE) {
    calld->deadline = op_deadline;
  }
  if (calld->host_set && calld->path_set) {
    /* do nothing */
  } else {
    /* Pass the error reference to calld->recv_initial_metadata_error */
    grpc_error* src_error = error;
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        "Missing :authority or :path", &src_error, 1);
    GRPC_ERROR_UNREF(src_error);
    calld->recv_initial_metadata_error = GRPC_ERROR_REF(error);
  }
  grpc_closure* closure = calld->on_done_recv_initial_metadata;
  calld->on_done_recv_initial_metadata = nullptr;
  if (calld->seen_recv_trailing_metadata_ready) {
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             &calld->recv_trailing_metadata_ready,
                             calld->recv_trailing_metadata_error,
                             "continue server_recv_trailing_metadata_ready");
  }
  Closure::Run(DEBUG_LOCATION, closure, error);
}

void server_recv_trailing_metadata_ready(void* user_data, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(user_data);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (calld->on_done_recv_initial_metadata != nullptr) {
    calld->recv_trailing_metadata_error = GRPC_ERROR_REF(error);
    calld->seen_recv_trailing_metadata_ready = true;
    GRPC_CLOSURE_INIT(&calld->recv_trailing_metadata_ready,
                      server_recv_trailing_metadata_ready, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_COMBINER_STOP(calld->call_combiner,
                            "deferring server_recv_trailing_metadata_ready "
                            "until after server_on_recv_initial_metadata");
    return;
  }
  error =
      grpc_error_add_child(GRPC_ERROR_REF(error),
                           GRPC_ERROR_REF(calld->recv_initial_metadata_error));
  Closure::Run(DEBUG_LOCATION, calld->original_recv_trailing_metadata_ready,
               error);
}

void server_mutate_op(grpc_call_element* elem,
                      grpc_transport_stream_op_batch* op) {
  call_data* calld = static_cast<call_data*>(elem->call_data);

  if (op->recv_initial_metadata) {
    GPR_ASSERT(op->payload->recv_initial_metadata.recv_flags == nullptr);
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->on_done_recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->on_recv_initial_metadata;
    op->payload->recv_initial_metadata.recv_flags =
        &calld->recv_initial_metadata_flags;
  }
  if (op->recv_trailing_metadata) {
    calld->original_recv_trailing_metadata_ready =
        op->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    op->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &calld->recv_trailing_metadata_ready;
  }
}

void server_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* op) {
  server_mutate_op(elem, op);
  grpc_call_next_op(elem, op);
}

void got_initial_metadata(void* ptr, grpc_error* error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(ptr);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (error == GRPC_ERROR_NONE) {
    start_new_rpc(elem);
  } else {
    CallState expect_not_started = CallState::NOT_STARTED;
    CallState expect_pending = CallState::PENDING;
    if (calld->state.CompareExchangeStrong(
            &expect_not_started, CallState::ZOMBIED,
            grpc_core::MemoryOrder::ACQ_REL, grpc_core::MemoryOrder::RELAXED)) {
      GRPC_CLOSURE_INIT(&calld->kill_zombie_closure, kill_zombie, elem,
                        grpc_schedule_on_exec_ctx);
      ExecCtx::Run(DEBUG_LOCATION, &calld->kill_zombie_closure,
                   GRPC_ERROR_NONE);
    } else if (calld->state.CompareExchangeStrong(
                   &expect_pending, CallState::ZOMBIED,
                   grpc_core::MemoryOrder::ACQ_REL,
                   grpc_core::MemoryOrder::RELAXED)) {
      /* zombied call will be destroyed when it's removed from the pending
         queue... later */
    }
  }
}

void accept_stream(void* cd, grpc_transport* /*transport*/,
                   const void* transport_server_data) {
  channel_data* chand = static_cast<channel_data*>(cd);
  /* create a call */
  grpc_call_create_args args;
  args.channel = chand->channel;
  args.server = chand->server;
  args.parent = nullptr;
  args.propagation_mask = 0;
  args.cq = nullptr;
  args.pollset_set_alternative = nullptr;
  args.server_transport_data = transport_server_data;
  args.add_initial_metadata = nullptr;
  args.add_initial_metadata_count = 0;
  args.send_deadline = GRPC_MILLIS_INF_FUTURE;
  grpc_call* call;
  grpc_error* error = grpc_call_create(&args, &call);
  grpc_call_element* elem =
      grpc_call_stack_element(grpc_call_get_call_stack(call), 0);
  if (error != GRPC_ERROR_NONE) {
    got_initial_metadata(elem, error);
    GRPC_ERROR_UNREF(error);
    return;
  }
  call_data* calld = static_cast<call_data*>(elem->call_data);
  grpc_op op;
  op.op = GRPC_OP_RECV_INITIAL_METADATA;
  op.flags = 0;
  op.reserved = nullptr;
  op.data.recv_initial_metadata.recv_initial_metadata =
      &calld->initial_metadata;
  GRPC_CLOSURE_INIT(&calld->got_initial_metadata, got_initial_metadata, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_call_start_batch_and_execute(call, &op, 1, &calld->got_initial_metadata);
}

grpc_error* server_init_call_elem(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  server_ref(chand->server);
  new (elem->call_data) call_data(elem, *args);
  return GRPC_ERROR_NONE;
}

void server_destroy_call_elem(grpc_call_element* elem,
                              const grpc_call_final_info* /*final_info*/,
                              grpc_closure* /*ignored*/) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->~call_data();
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  server_unref(chand->server);
}

grpc_error* server_init_channel_elem(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(args->is_first);
  GPR_ASSERT(!args->is_last);

  new (static_cast<channel_data*>(elem->channel_data)) channel_data;
  return GRPC_ERROR_NONE;
}

channel_data::~channel_data() {
  if (registered_methods) {
    for (const channel_registered_method& crm : *registered_methods) {
      grpc_slice_unref_internal(crm.method);
      GPR_DEBUG_ASSERT(crm.method.refcount == &kNoopRefcount ||
                       crm.method.refcount == nullptr);
      if (crm.has_host) {
        grpc_slice_unref_internal(crm.host);
        GPR_DEBUG_ASSERT(crm.host.refcount == &kNoopRefcount ||
                         crm.host.refcount == nullptr);
      }
    }
  }
  if (server) {
    if (server->channelz_server != nullptr && channelz_socket_uuid != 0) {
      server->channelz_server->RemoveChildSocket(channelz_socket_uuid);
    }
    {
      MutexLock lock(&server->mu_global);
      if (list_position.has_value()) {
        server->channels.erase(*list_position);
      }
      maybe_finish_shutdown(server);
    }
    server_unref(server);
  }
}

void server_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->~channel_data();
}

void register_completion_queue(grpc_server* server, grpc_completion_queue* cq,
                               void* reserved) {
  size_t i;
  GPR_ASSERT(!reserved);
  for (i = 0; i < server->cqs.size(); i++) {
    if (server->cqs[i] == cq) return;
  }

  GRPC_CQ_INTERNAL_REF(cq, "server");
  server->cqs.push_back(cq);
}

bool streq(const std::string& a, const char* b) {
  return (a.empty() && b == nullptr) ||
         ((b != nullptr) && !strcmp(a.c_str(), b));
}

class ConnectivityWatcher : public AsyncConnectivityStateWatcherInterface {
 public:
  explicit ConnectivityWatcher(channel_data* chand) : chand_(chand) {
    GRPC_CHANNEL_INTERNAL_REF(chand_->channel, "connectivity");
  }

  ~ConnectivityWatcher() {
    GRPC_CHANNEL_INTERNAL_UNREF(chand_->channel, "connectivity");
  }

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state) override {
    // Don't do anything until we are being shut down.
    if (new_state != GRPC_CHANNEL_SHUTDOWN) return;
    // Shut down channel.
    grpc_server* server = chand_->server;
    MutexLock lock(&server->mu_global);
    destroy_channel(chand_);
  }

  channel_data* chand_;
};

void done_published_shutdown(void* /*done_arg*/, grpc_cq_completion* storage) {
  delete storage;
}

void listener_destroy_done(void* s, grpc_error* /*error*/) {
  grpc_server* server = static_cast<grpc_server*>(s);
  MutexLock lock(&server->mu_global);
  server->listeners_destroyed++;
  maybe_finish_shutdown(server);
}

grpc_call_error queue_call_request(grpc_server* server, size_t cq_idx,
                                   requested_call* rc) {
  if (server->shutdown_flag.load(std::memory_order_acquire)) {
    fail_call(server, cq_idx, rc,
              GRPC_ERROR_CREATE_FROM_STATIC_STRING("Server Shutdown"));
    return GRPC_CALL_OK;
  }
  RequestMatcherInterface* rm;
  switch (rc->type) {
    case RequestedCallType::BATCH_CALL:
      rm = server->unregistered_request_matcher.get();
      break;
    case RequestedCallType::REGISTERED_CALL:
      rm = rc->data.registered.method->matcher.get();
      break;
  }
  rm->RequestCallWithPossiblePublish(cq_idx, rc);
  return GRPC_CALL_OK;
}

void fail_call(grpc_server* server, size_t cq_idx, requested_call* rc,
               grpc_error* error) {
  *rc->call = nullptr;
  rc->initial_metadata->count = 0;
  GPR_ASSERT(error != GRPC_ERROR_NONE);

  grpc_cq_end_op(server->cqs[cq_idx], rc->tag, error, done_request_event, rc,
                 &rc->completion);
}

}  // namespace

void SetServerRegisteredMethodAllocator(
    grpc_server* server, grpc_completion_queue* cq, void* method_tag,
    std::function<ServerRegisteredCallAllocation()> allocator) {
  registered_method* rm = static_cast<registered_method*>(method_tag);
  rm->matcher = absl::make_unique<AllocatingRequestMatcherRegistered>(
      server, cq, rm, std::move(allocator));
}

void SetServerBatchMethodAllocator(
    grpc_server* server, grpc_completion_queue* cq,
    std::function<ServerBatchCallAllocation()> allocator) {
  GPR_DEBUG_ASSERT(server->unregistered_request_matcher == nullptr);
  server->unregistered_request_matcher =
      absl::make_unique<AllocatingRequestMatcherBatch>(server, cq,
                                                       std::move(allocator));
}

}  // namespace grpc_core

const grpc_channel_filter grpc_server_top_filter = {
    grpc_core::server_start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(grpc_core::call_data),
    grpc_core::server_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    grpc_core::server_destroy_call_elem,
    sizeof(grpc_core::channel_data),
    grpc_core::server_init_channel_elem,
    grpc_core::server_destroy_channel_elem,
    grpc_channel_next_get_info,
    "server",
};

// The following are core surface API functions.

void grpc_server_register_completion_queue(grpc_server* server,
                                           grpc_completion_queue* cq,
                                           void* reserved) {
  GRPC_API_TRACE(
      "grpc_server_register_completion_queue(server=%p, cq=%p, reserved=%p)", 3,
      (server, cq, reserved));

  auto cq_type = grpc_get_cq_completion_type(cq);
  if (cq_type != GRPC_CQ_NEXT && cq_type != GRPC_CQ_CALLBACK) {
    gpr_log(GPR_INFO,
            "Completion queue of type %d is being registered as a "
            "server-completion-queue",
            static_cast<int>(cq_type));
    /* Ideally we should log an error and abort but ruby-wrapped-language API
       calls grpc_completion_queue_pluck() on server completion queues */
  }

  grpc_core::register_completion_queue(server, cq, reserved);
}

grpc_server* grpc_server_create(const grpc_channel_args* args, void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE("grpc_server_create(%p, %p)", 2, (args, reserved));

  return new grpc_server(args);
}

void* grpc_server_register_method(
    grpc_server* server, const char* method, const char* host,
    grpc_server_register_method_payload_handling payload_handling,
    uint32_t flags) {
  GRPC_API_TRACE(
      "grpc_server_register_method(server=%p, method=%s, host=%s, "
      "flags=0x%08x)",
      4, (server, method, host, flags));
  if (!method) {
    gpr_log(GPR_ERROR,
            "grpc_server_register_method method string cannot be NULL");
    return nullptr;
  }
  for (std::unique_ptr<grpc_core::registered_method>& m :
       server->registered_methods) {
    if (grpc_core::streq(m->method, method) &&
        grpc_core::streq(m->host, host)) {
      gpr_log(GPR_ERROR, "duplicate registration for %s@%s", method,
              host ? host : "*");
      return nullptr;
    }
  }
  if ((flags & ~GRPC_INITIAL_METADATA_USED_MASK) != 0) {
    gpr_log(GPR_ERROR, "grpc_server_register_method invalid flags 0x%08x",
            flags);
    return nullptr;
  }
  server->registered_methods.emplace_back(
      new grpc_core::registered_method(method, host, payload_handling, flags));
  return static_cast<void*>(server->registered_methods.back().get());
}

void grpc_server_start(grpc_server* server) {
  size_t i;
  grpc_core::ExecCtx exec_ctx;

  GRPC_API_TRACE("grpc_server_start(server=%p)", 1, (server));

  server->started = true;
  for (i = 0; i < server->cqs.size(); i++) {
    if (grpc_cq_can_listen(server->cqs[i])) {
      server->pollsets.push_back(grpc_cq_pollset(server->cqs[i]));
    }
  }
  if (server->unregistered_request_matcher == nullptr) {
    server->unregistered_request_matcher =
        absl::make_unique<grpc_core::RealRequestMatcher>(server);
  }
  for (std::unique_ptr<grpc_core::registered_method>& rm :
       server->registered_methods) {
    if (rm->matcher == nullptr) {
      rm->matcher = absl::make_unique<grpc_core::RealRequestMatcher>(server);
    }
  }

  {
    grpc_core::MutexLock lock(&server->mu_global);
    server->starting = true;
  }

  for (auto& listener : server->listeners) {
    listener.listener->Start(server, &server->pollsets);
  }

  grpc_core::MutexLock lock(&server->mu_global);
  server->starting = false;
  server->starting_cv.Signal();
}

/*
  - Kills all pending requests-for-incoming-RPC-calls (i.e the requests made via
    grpc_server_request_call and grpc_server_request_registered call will now be
    cancelled). See 'kill_pending_work_locked()'

  - Shuts down the listeners (i.e the server will no longer listen on the port
    for new incoming channels).

  - Iterates through all channels on the server and sends shutdown msg (see
    'ChannelBroadcaster::BroadcastShutdown' for details) to the clients via the
    transport layer. The transport layer then guarantees the following:
     -- Sends shutdown to the client (for eg: HTTP2 transport sends GOAWAY)
     -- If the server has outstanding calls that are in the process, the
        connection is NOT closed until the server is done with all those calls
     -- Once, there are no more calls in progress, the channel is closed
 */
void grpc_server_shutdown_and_notify(grpc_server* server,
                                     grpc_completion_queue* cq, void* tag) {
  grpc_core::ChannelBroadcaster broadcaster;
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;

  GRPC_API_TRACE("grpc_server_shutdown_and_notify(server=%p, cq=%p, tag=%p)", 3,
                 (server, cq, tag));

  {
    /* wait for startup to be finished: locks mu_global */
    grpc_core::MutexLock lock(&server->mu_global);
    server->starting_cv.WaitUntil(&server->mu_global,
                                  [server] { return !server->starting; });

    /* stay locked, and gather up some stuff to do */
    GPR_ASSERT(grpc_cq_begin_op(cq, tag));
    if (server->shutdown_published) {
      grpc_cq_end_op(cq, tag, GRPC_ERROR_NONE,
                     grpc_core::done_published_shutdown, nullptr,
                     new grpc_cq_completion);
      return;
    }
    server->shutdown_tags.emplace_back(tag, cq);
    if (server->shutdown_flag.load(std::memory_order_acquire)) {
      return;
    }

    server->last_shutdown_message_time = gpr_now(GPR_CLOCK_REALTIME);

    broadcaster.FillChannelsLocked(server);

    server->shutdown_flag.store(true, std::memory_order_release);

    /* collect all unregistered then registered calls */
    {
      grpc_core::MutexLock lock(&server->mu_call);
      grpc_core::kill_pending_work_locked(
          server, GRPC_ERROR_CREATE_FROM_STATIC_STRING("Server Shutdown"));
    }

    grpc_core::maybe_finish_shutdown(server);
  }

  /* Shutdown listeners */
  for (auto& listener : server->listeners) {
    grpc_core::channelz::ListenSocketNode* channelz_listen_socket_node =
        listener.listener->channelz_listen_socket_node();
    if (server->channelz_server != nullptr &&
        channelz_listen_socket_node != nullptr) {
      server->channelz_server->RemoveChildListenSocket(
          channelz_listen_socket_node->uuid());
    }
    GRPC_CLOSURE_INIT(&listener.destroy_done, grpc_core::listener_destroy_done,
                      server, grpc_schedule_on_exec_ctx);
    listener.listener->SetOnDestroyDone(&listener.destroy_done);
    listener.listener.reset();
  }

  broadcaster.BroadcastShutdown(/*send_goaway=*/true, GRPC_ERROR_NONE);
}

void grpc_server_cancel_all_calls(grpc_server* server) {
  grpc_core::ChannelBroadcaster broadcaster;
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;

  GRPC_API_TRACE("grpc_server_cancel_all_calls(server=%p)", 1, (server));

  {
    grpc_core::MutexLock lock(&server->mu_global);
    broadcaster.FillChannelsLocked(server);
  }

  broadcaster.BroadcastShutdown(
      /*send_goaway=*/false,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING("Cancelling all calls"));
}

void grpc_server_destroy(grpc_server* server) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;

  GRPC_API_TRACE("grpc_server_destroy(server=%p)", 1, (server));

  {
    grpc_core::MutexLock lock(&server->mu_global);
    GPR_ASSERT(server->shutdown_flag.load(std::memory_order_acquire) ||
               server->listeners.empty());
    GPR_ASSERT(server->listeners_destroyed == server->listeners.size());
  }

  if (server->default_resource_user != nullptr) {
    grpc_resource_quota_unref(
        grpc_resource_user_quota(server->default_resource_user));
    grpc_resource_user_shutdown(server->default_resource_user);
    grpc_resource_user_unref(server->default_resource_user);
  }
  grpc_core::server_unref(server);
}

const std::vector<grpc_pollset*>& grpc_server_get_pollsets(
    grpc_server* server) {
  return server->pollsets;
}

void grpc_server_setup_transport(
    grpc_server* s, grpc_transport* transport, grpc_pollset* accepting_pollset,
    const grpc_channel_args* args,
    const grpc_core::RefCountedPtr<grpc_core::channelz::SocketNode>&
        socket_node,
    grpc_resource_user* resource_user) {
  size_t num_registered_methods;
  grpc_core::channel_registered_method* crm;
  grpc_channel* channel;
  grpc_core::channel_data* chand;
  uint32_t hash;
  size_t slots;
  uint32_t probes;
  uint32_t max_probes = 0;
  grpc_transport_op* op = nullptr;

  channel = grpc_channel_create(nullptr, args, GRPC_SERVER_CHANNEL, transport,
                                resource_user);
  chand = static_cast<grpc_core::channel_data*>(
      grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0)
          ->channel_data);
  chand->server = s;
  grpc_core::server_ref(s);
  chand->channel = channel;
  if (socket_node != nullptr) {
    chand->channelz_socket_uuid = socket_node->uuid();
    s->channelz_server->AddChildSocket(socket_node);
  } else {
    chand->channelz_socket_uuid = 0;
  }

  size_t cq_idx;
  for (cq_idx = 0; cq_idx < s->cqs.size(); cq_idx++) {
    if (grpc_cq_pollset(s->cqs[cq_idx]) == accepting_pollset) break;
  }
  if (cq_idx == s->cqs.size()) {
    /* completion queue not found: pick a random one to publish new calls to */
    cq_idx = static_cast<size_t>(rand()) % s->cqs.size();
  }
  chand->cq_idx = cq_idx;

  num_registered_methods = s->registered_methods.size();
  /* build a lookup table phrased in terms of mdstr's in this channels context
     to quickly find registered methods */
  if (num_registered_methods > 0) {
    slots = 2 * num_registered_methods;
    chand->registered_methods.reset(
        new std::vector<grpc_core::channel_registered_method>(slots));
    for (std::unique_ptr<grpc_core::registered_method>& rm :
         s->registered_methods) {
      grpc_core::ExternallyManagedSlice host;
      grpc_core::ExternallyManagedSlice method(rm->method.c_str());
      const bool has_host = !rm->host.empty();
      if (has_host) {
        host = grpc_core::ExternallyManagedSlice(rm->host.c_str());
      }
      hash = GRPC_MDSTR_KV_HASH(has_host ? host.Hash() : 0, method.Hash());
      for (probes = 0; (*chand->registered_methods)[(hash + probes) % slots]
                           .server_registered_method != nullptr;
           probes++) {
      }
      if (probes > max_probes) max_probes = probes;
      crm = &(*chand->registered_methods)[(hash + probes) % slots];
      crm->server_registered_method = rm.get();
      crm->flags = rm->flags;
      crm->has_host = has_host;
      if (has_host) {
        crm->host = host;
      }
      crm->method = method;
    }
    GPR_ASSERT(slots <= UINT32_MAX);
    chand->registered_method_max_probes = max_probes;
  }

  {
    grpc_core::MutexLock lock(&s->mu_global);
    s->channels.push_front(chand);
    chand->list_position = s->channels.begin();
  }

  op = grpc_make_transport_op(nullptr);
  op->set_accept_stream = true;
  op->set_accept_stream_fn = grpc_core::accept_stream;
  op->set_accept_stream_user_data = chand;
  op->start_connectivity_watch.reset(new grpc_core::ConnectivityWatcher(chand));
  if (s->shutdown_flag.load(std::memory_order_acquire)) {
    op->disconnect_with_error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Server shutdown");
  }
  grpc_transport_perform_op(transport, op);
}

grpc_call_error grpc_server_request_call(
    grpc_server* server, grpc_call** call, grpc_call_details* details,
    grpc_metadata_array* initial_metadata,
    grpc_completion_queue* cq_bound_to_call,
    grpc_completion_queue* cq_for_notification, void* tag) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_STATS_INC_SERVER_REQUESTED_CALLS();
  GRPC_API_TRACE(
      "grpc_server_request_call("
      "server=%p, call=%p, details=%p, initial_metadata=%p, "
      "cq_bound_to_call=%p, cq_for_notification=%p, tag=%p)",
      7,
      (server, call, details, initial_metadata, cq_bound_to_call,
       cq_for_notification, tag));

  size_t cq_idx;
  grpc_call_error error = grpc_core::ValidateServerRequestAndCq(
      &cq_idx, server, cq_for_notification, tag, nullptr, nullptr);
  if (error != GRPC_CALL_OK) {
    return error;
  }

  grpc_core::requested_call* rc = new grpc_core::requested_call(
      tag, cq_bound_to_call, call, initial_metadata, details);
  return queue_call_request(server, cq_idx, rc);
}

grpc_call_error grpc_server_request_registered_call(
    grpc_server* server, void* rmp, grpc_call** call, gpr_timespec* deadline,
    grpc_metadata_array* initial_metadata, grpc_byte_buffer** optional_payload,
    grpc_completion_queue* cq_bound_to_call,
    grpc_completion_queue* cq_for_notification, void* tag_new) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_STATS_INC_SERVER_REQUESTED_CALLS();
  grpc_core::registered_method* rm =
      static_cast<grpc_core::registered_method*>(rmp);
  GRPC_API_TRACE(
      "grpc_server_request_registered_call("
      "server=%p, rmp=%p, call=%p, deadline=%p, initial_metadata=%p, "
      "optional_payload=%p, cq_bound_to_call=%p, cq_for_notification=%p, "
      "tag=%p)",
      9,
      (server, rmp, call, deadline, initial_metadata, optional_payload,
       cq_bound_to_call, cq_for_notification, tag_new));

  size_t cq_idx;
  grpc_call_error error = ValidateServerRequestAndCq(
      &cq_idx, server, cq_for_notification, tag_new, optional_payload, rm);
  if (error != GRPC_CALL_OK) {
    return error;
  }

  grpc_core::requested_call* rc = new grpc_core::requested_call(
      tag_new, cq_bound_to_call, call, initial_metadata, rm, deadline,
      optional_payload);
  return queue_call_request(server, cq_idx, rc);
}
