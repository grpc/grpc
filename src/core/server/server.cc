//
// Copyright 2015-2016 gRPC authors.
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

#include "src/core/server/server.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <list>
#include <memory>
#include <new>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "src/core/channelz/channel_trace.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/mpscq.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_utils.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/legacy_channel.h"
#include "src/core/lib/surface/server_call.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/interception_chain.h"
#include "src/core/telemetry/stats.h"
#include "src/core/util/useful.h"

namespace grpc_core {

//
// Server::RequestMatcherInterface
//

// RPCs that come in from the transport must be matched against RPC requests
// from the application. An incoming request from the application can be matched
// to an RPC that has already arrived or can be queued up for later use.
// Likewise, an RPC coming in from the transport can either be matched to a
// request that already arrived from the application or can be queued up for
// later use (marked pending). If there is a match, the request's tag is posted
// on the request's notification CQ.
//
// RequestMatcherInterface is the base class to provide this functionality.
class Server::RequestMatcherInterface {
 public:
  virtual ~RequestMatcherInterface() {}

  // Unref the calls associated with any incoming RPCs in the pending queue (not
  // yet matched to an application-requested RPC).
  virtual void ZombifyPending() = 0;

  // Mark all application-requested RPCs failed if they have not been matched to
  // an incoming RPC. The error parameter indicates why the RPCs are being
  // failed (always server shutdown in all current implementations).
  virtual void KillRequests(grpc_error_handle error) = 0;

  // How many request queues are supported by this matcher. This is an abstract
  // concept that essentially maps to gRPC completion queues.
  virtual size_t request_queue_count() const = 0;

  // This function is invoked when the application requests a new RPC whose
  // information is in the call parameter. The request_queue_index marks the
  // queue onto which to place this RPC, and is typically associated with a gRPC
  // CQ. If there are pending RPCs waiting to be matched, publish one (match it
  // and notify the CQ).
  virtual void RequestCallWithPossiblePublish(size_t request_queue_index,
                                              RequestedCall* call) = 0;

  class MatchResult {
   public:
    MatchResult(Server* server, size_t cq_idx, RequestedCall* requested_call)
        : server_(server), cq_idx_(cq_idx), requested_call_(requested_call) {}
    ~MatchResult() {
      if (requested_call_ != nullptr) {
        server_->FailCall(cq_idx_, requested_call_, absl::CancelledError());
      }
    }

    MatchResult(const MatchResult&) = delete;
    MatchResult& operator=(const MatchResult&) = delete;

    MatchResult(MatchResult&& other) noexcept
        : server_(other.server_),
          cq_idx_(other.cq_idx_),
          requested_call_(std::exchange(other.requested_call_, nullptr)) {}

    RequestedCall* TakeCall() {
      return std::exchange(requested_call_, nullptr);
    }

    grpc_completion_queue* cq() const { return server_->cqs_[cq_idx_]; }
    size_t cq_idx() const { return cq_idx_; }

   private:
    Server* server_;
    size_t cq_idx_;
    RequestedCall* requested_call_;
  };

  // This function is invoked on an incoming promise based RPC.
  // The RequestMatcher will try to match it against an application-requested
  // RPC if possible or will place it in the pending queue otherwise. To enable
  // some measure of fairness between server CQs, the match is done starting at
  // the start_request_queue_index parameter in a cyclic order rather than
  // always starting at 0.
  virtual ArenaPromise<absl::StatusOr<MatchResult>> MatchRequest(
      size_t start_request_queue_index) = 0;

  // This function is invoked on an incoming RPC, represented by the calld
  // object. The RequestMatcher will try to match it against an
  // application-requested RPC if possible or will place it in the pending queue
  // otherwise. To enable some measure of fairness between server CQs, the match
  // is done starting at the start_request_queue_index parameter in a cyclic
  // order rather than always starting at 0.
  virtual void MatchOrQueue(size_t start_request_queue_index,
                            CallData* calld) = 0;

  // Returns the server associated with this request matcher
  virtual Server* server() const = 0;
};

//
// Server::RegisteredMethod
//

struct Server::RegisteredMethod {
  RegisteredMethod(
      const char* method_arg, const char* host_arg,
      grpc_server_register_method_payload_handling payload_handling_arg,
      uint32_t flags_arg)
      : method(method_arg == nullptr ? "" : method_arg),
        host(host_arg == nullptr ? "" : host_arg),
        payload_handling(payload_handling_arg),
        flags(flags_arg) {}

  ~RegisteredMethod() = default;

  const std::string method;
  const std::string host;
  const grpc_server_register_method_payload_handling payload_handling;
  const uint32_t flags;
  // One request matcher per method.
  std::unique_ptr<RequestMatcherInterface> matcher;
};

//
// Server::RequestedCall
//

struct Server::RequestedCall {
  enum class Type { BATCH_CALL, REGISTERED_CALL };

  RequestedCall(void* tag_arg, grpc_completion_queue* call_cq,
                grpc_call** call_arg, grpc_metadata_array* initial_md,
                grpc_call_details* details)
      : type(Type::BATCH_CALL),
        tag(tag_arg),
        cq_bound_to_call(call_cq),
        call(call_arg),
        initial_metadata(initial_md) {
    data.batch.details = details;
  }

  RequestedCall(void* tag_arg, grpc_completion_queue* call_cq,
                grpc_call** call_arg, grpc_metadata_array* initial_md,
                RegisteredMethod* rm, gpr_timespec* deadline,
                grpc_byte_buffer** optional_payload)
      : type(Type::REGISTERED_CALL),
        tag(tag_arg),
        cq_bound_to_call(call_cq),
        call(call_arg),
        initial_metadata(initial_md) {
    data.registered.method = rm;
    data.registered.deadline = deadline;
    data.registered.optional_payload = optional_payload;
  }

  template <typename OptionalPayload>
  void Complete(OptionalPayload payload, ClientMetadata& md) {
    Timestamp deadline =
        md.get(GrpcTimeoutMetadata()).value_or(Timestamp::InfFuture());
    switch (type) {
      case RequestedCall::Type::BATCH_CALL:
        CHECK(!payload.has_value());
        data.batch.details->host =
            CSliceRef(md.get_pointer(HttpAuthorityMetadata())->c_slice());
        data.batch.details->method =
            CSliceRef(md.Take(HttpPathMetadata())->c_slice());
        data.batch.details->deadline =
            deadline.as_timespec(GPR_CLOCK_MONOTONIC);
        break;
      case RequestedCall::Type::REGISTERED_CALL:
        md.Remove(HttpPathMetadata());
        *data.registered.deadline = deadline.as_timespec(GPR_CLOCK_MONOTONIC);
        if (data.registered.optional_payload != nullptr) {
          if (payload.has_value()) {
            auto* sb = payload.value()->payload()->c_slice_buffer();
            *data.registered.optional_payload =
                grpc_raw_byte_buffer_create(sb->slices, sb->count);
          } else {
            *data.registered.optional_payload = nullptr;
          }
        }
        break;
      default:
        GPR_UNREACHABLE_CODE(abort());
    }
  }

  MultiProducerSingleConsumerQueue::Node mpscq_node;
  const Type type;
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
      RegisteredMethod* method;
      gpr_timespec* deadline;
      grpc_byte_buffer** optional_payload;
    } registered;
  } data;
};

// The RealRequestMatcher is an implementation of RequestMatcherInterface that
// actually uses all the features of RequestMatcherInterface: expecting the
// application to explicitly request RPCs and then matching those to incoming
// RPCs, along with a slow path by which incoming RPCs are put on a locked
// pending list if they aren't able to be matched to an application request.
class Server::RealRequestMatcher : public RequestMatcherInterface {
 public:
  explicit RealRequestMatcher(Server* server)
      : server_(server), requests_per_cq_(server->cqs_.size()) {}

  ~RealRequestMatcher() override {
    for (LockedMultiProducerSingleConsumerQueue& queue : requests_per_cq_) {
      CHECK_EQ(queue.Pop(), nullptr);
    }
    CHECK(pending_filter_stack_.empty());
    CHECK(pending_promises_.empty());
  }

  void ZombifyPending() override {
    while (!pending_filter_stack_.empty()) {
      pending_filter_stack_.front().calld->SetState(
          CallData::CallState::ZOMBIED);
      pending_filter_stack_.front().calld->KillZombie();
      pending_filter_stack_.pop();
    }
    while (!pending_promises_.empty()) {
      pending_promises_.front()->Finish(absl::InternalError("Server closed"));
      pending_promises_.pop();
    }
  }

  void KillRequests(grpc_error_handle error) override {
    for (size_t i = 0; i < requests_per_cq_.size(); i++) {
      RequestedCall* rc;
      while ((rc = reinterpret_cast<RequestedCall*>(
                  requests_per_cq_[i].Pop())) != nullptr) {
        server_->FailCall(i, rc, error);
      }
    }
  }

  size_t request_queue_count() const override {
    return requests_per_cq_.size();
  }

  void RequestCallWithPossiblePublish(size_t request_queue_index,
                                      RequestedCall* call) override {
    if (requests_per_cq_[request_queue_index].Push(&call->mpscq_node)) {
      // this was the first queued request: we need to lock and start
      // matching calls
      struct NextPendingCall {
        RequestedCall* rc = nullptr;
        CallData* pending_filter_stack = nullptr;
        PendingCallPromises pending_promise;
      };
      while (true) {
        NextPendingCall pending_call;
        {
          MutexLock lock(&server_->mu_call_);
          while (!pending_filter_stack_.empty() &&
                 pending_filter_stack_.front().Age() >
                     server_->max_time_in_pending_queue_) {
            pending_filter_stack_.front().calld->SetState(
                CallData::CallState::ZOMBIED);
            pending_filter_stack_.front().calld->KillZombie();
            pending_filter_stack_.pop();
          }
          if (!pending_promises_.empty()) {
            pending_call.rc = reinterpret_cast<RequestedCall*>(
                requests_per_cq_[request_queue_index].Pop());
            if (pending_call.rc != nullptr) {
              pending_call.pending_promise =
                  std::move(pending_promises_.front());
              pending_promises_.pop();
            }
          } else if (!pending_filter_stack_.empty()) {
            pending_call.rc = reinterpret_cast<RequestedCall*>(
                requests_per_cq_[request_queue_index].Pop());
            if (pending_call.rc != nullptr) {
              pending_call.pending_filter_stack =
                  pending_filter_stack_.front().calld;
              pending_filter_stack_.pop();
            }
          }
        }
        if (pending_call.rc == nullptr) break;
        if (pending_call.pending_filter_stack != nullptr) {
          if (!pending_call.pending_filter_stack->MaybeActivate()) {
            // Zombied Call
            pending_call.pending_filter_stack->KillZombie();
            requests_per_cq_[request_queue_index].Push(
                &pending_call.rc->mpscq_node);
          } else {
            pending_call.pending_filter_stack->Publish(request_queue_index,
                                                       pending_call.rc);
          }
        } else {
          if (!pending_call.pending_promise->Finish(
                  server(), request_queue_index, pending_call.rc)) {
            requests_per_cq_[request_queue_index].Push(
                &pending_call.rc->mpscq_node);
          }
        }
      }
    }
  }

  void MatchOrQueue(size_t start_request_queue_index,
                    CallData* calld) override {
    for (size_t i = 0; i < requests_per_cq_.size(); i++) {
      size_t cq_idx = (start_request_queue_index + i) % requests_per_cq_.size();
      RequestedCall* rc =
          reinterpret_cast<RequestedCall*>(requests_per_cq_[cq_idx].TryPop());
      if (rc != nullptr) {
        calld->SetState(CallData::CallState::ACTIVATED);
        calld->Publish(cq_idx, rc);
        return;
      }
    }
    // No cq to take the request found; queue it on the slow list.
    // We need to ensure that all the queues are empty.  We do this under
    // the server mu_call_ lock to ensure that if something is added to
    // an empty request queue, it will block until the call is actually
    // added to the pending list.
    RequestedCall* rc = nullptr;
    size_t cq_idx = 0;
    size_t loop_count;
    {
      MutexLock lock(&server_->mu_call_);
      for (loop_count = 0; loop_count < requests_per_cq_.size(); loop_count++) {
        cq_idx =
            (start_request_queue_index + loop_count) % requests_per_cq_.size();
        rc = reinterpret_cast<RequestedCall*>(requests_per_cq_[cq_idx].Pop());
        if (rc != nullptr) {
          break;
        }
      }
      if (rc == nullptr) {
        calld->SetState(CallData::CallState::PENDING);
        pending_filter_stack_.push(PendingCallFilterStack{calld});
        return;
      }
    }
    calld->SetState(CallData::CallState::ACTIVATED);
    calld->Publish(cq_idx, rc);
  }

  ArenaPromise<absl::StatusOr<MatchResult>> MatchRequest(
      size_t start_request_queue_index) override {
    for (size_t i = 0; i < requests_per_cq_.size(); i++) {
      size_t cq_idx = (start_request_queue_index + i) % requests_per_cq_.size();
      RequestedCall* rc =
          reinterpret_cast<RequestedCall*>(requests_per_cq_[cq_idx].TryPop());
      if (rc != nullptr) {
        return Immediate(MatchResult(server(), cq_idx, rc));
      }
    }
    // No cq to take the request found; queue it on the slow list.
    // We need to ensure that all the queues are empty.  We do this under
    // the server mu_call_ lock to ensure that if something is added to
    // an empty request queue, it will block until the call is actually
    // added to the pending list.
    RequestedCall* rc = nullptr;
    size_t cq_idx = 0;
    size_t loop_count;
    {
      std::vector<std::shared_ptr<ActivityWaiter>> removed_pending;
      MutexLock lock(&server_->mu_call_);
      while (!pending_promises_.empty() &&
             pending_promises_.front()->Age() >
                 server_->max_time_in_pending_queue_) {
        removed_pending.push_back(std::move(pending_promises_.front()));
        pending_promises_.pop();
      }
      for (loop_count = 0; loop_count < requests_per_cq_.size(); loop_count++) {
        cq_idx =
            (start_request_queue_index + loop_count) % requests_per_cq_.size();
        rc = reinterpret_cast<RequestedCall*>(requests_per_cq_[cq_idx].Pop());
        if (rc != nullptr) break;
      }
      if (rc == nullptr) {
        if (server_->pending_backlog_protector_.Reject(pending_promises_.size(),
                                                       server_->bitgen_)) {
          return Immediate(absl::ResourceExhaustedError(
              "Too many pending requests for this server"));
        }
        auto w = std::make_shared<ActivityWaiter>(
            GetContext<Activity>()->MakeOwningWaker());
        pending_promises_.push(w);
        return OnCancel(
            [w]() -> Poll<absl::StatusOr<MatchResult>> {
              std::unique_ptr<absl::StatusOr<MatchResult>> r(
                  w->result.exchange(nullptr, std::memory_order_acq_rel));
              if (r == nullptr) return Pending{};
              return std::move(*r);
            },
            [w]() { w->Expire(); });
      }
    }
    return Immediate(MatchResult(server(), cq_idx, rc));
  }

  Server* server() const final { return server_; }

 private:
  Server* const server_;
  struct PendingCallFilterStack {
    CallData* calld;
    Timestamp created = Timestamp::Now();
    Duration Age() { return Timestamp::Now() - created; }
  };
  struct ActivityWaiter {
    using ResultType = absl::StatusOr<MatchResult>;
    explicit ActivityWaiter(Waker waker) : waker(std::move(waker)) {}
    ~ActivityWaiter() { delete result.load(std::memory_order_acquire); }
    void Finish(absl::Status status) {
      delete result.exchange(new ResultType(std::move(status)),
                             std::memory_order_acq_rel);
      waker.WakeupAsync();
    }
    // Returns true if requested_call consumed, false otherwise.
    GRPC_MUST_USE_RESULT bool Finish(Server* server, size_t cq_idx,
                                     RequestedCall* requested_call) {
      ResultType* expected = nullptr;
      ResultType* new_value =
          new ResultType(MatchResult(server, cq_idx, requested_call));
      if (!result.compare_exchange_strong(expected, new_value,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
        CHECK(new_value->value().TakeCall() == requested_call);
        delete new_value;
        return false;
      }
      waker.WakeupAsync();
      return true;
    }
    void Expire() {
      delete result.exchange(new ResultType(absl::CancelledError()),
                             std::memory_order_acq_rel);
    }
    Duration Age() { return Timestamp::Now() - created; }
    Waker waker;
    std::atomic<ResultType*> result{nullptr};
    const Timestamp created = Timestamp::Now();
  };
  using PendingCallPromises = std::shared_ptr<ActivityWaiter>;
  std::queue<PendingCallFilterStack> pending_filter_stack_;
  std::queue<PendingCallPromises> pending_promises_;
  std::vector<LockedMultiProducerSingleConsumerQueue> requests_per_cq_;
};

// AllocatingRequestMatchers don't allow the application to request an RPC in
// advance or queue up any incoming RPC for later match. Instead, MatchOrQueue
// will call out to an allocation function passed in at the construction of the
// object. These request matchers are designed for the C++ callback API, so they
// only support 1 completion queue (passed in at the constructor). They are also
// used for the sync API.
class Server::AllocatingRequestMatcherBase : public RequestMatcherInterface {
 public:
  AllocatingRequestMatcherBase(Server* server, grpc_completion_queue* cq)
      : server_(server), cq_(cq) {
    size_t idx;
    for (idx = 0; idx < server->cqs_.size(); idx++) {
      if (server->cqs_[idx] == cq) {
        break;
      }
    }
    CHECK(idx < server->cqs_.size());
    cq_idx_ = idx;
  }

  void ZombifyPending() override {}

  void KillRequests(grpc_error_handle /*error*/) override {}

  size_t request_queue_count() const override { return 0; }

  void RequestCallWithPossiblePublish(size_t /*request_queue_index*/,
                                      RequestedCall* /*call*/) final {
    Crash("unreachable");
  }

  Server* server() const final { return server_; }

  // Supply the completion queue related to this request matcher
  grpc_completion_queue* cq() const { return cq_; }

  // Supply the completion queue's index relative to the server.
  size_t cq_idx() const { return cq_idx_; }

 private:
  Server* const server_;
  grpc_completion_queue* const cq_;
  size_t cq_idx_;
};

// An allocating request matcher for non-registered methods (used for generic
// API and unimplemented RPCs).
class Server::AllocatingRequestMatcherBatch
    : public AllocatingRequestMatcherBase {
 public:
  AllocatingRequestMatcherBatch(Server* server, grpc_completion_queue* cq,
                                std::function<BatchCallAllocation()> allocator)
      : AllocatingRequestMatcherBase(server, cq),
        allocator_(std::move(allocator)) {}

  void MatchOrQueue(size_t /*start_request_queue_index*/,
                    CallData* calld) override {
    const bool still_running = server()->ShutdownRefOnRequest();
    auto cleanup_ref =
        absl::MakeCleanup([this] { server()->ShutdownUnrefOnRequest(); });
    if (still_running) {
      BatchCallAllocation call_info = allocator_();
      CHECK(server()->ValidateServerRequest(cq(),
                                            static_cast<void*>(call_info.tag),
                                            nullptr, nullptr) == GRPC_CALL_OK);
      RequestedCall* rc = new RequestedCall(
          static_cast<void*>(call_info.tag), call_info.cq, call_info.call,
          call_info.initial_metadata, call_info.details);
      calld->SetState(CallData::CallState::ACTIVATED);
      calld->Publish(cq_idx(), rc);
    } else {
      calld->FailCallCreation();
    }
  }

  ArenaPromise<absl::StatusOr<MatchResult>> MatchRequest(
      size_t /*start_request_queue_index*/) override {
    BatchCallAllocation call_info = allocator_();
    CHECK(server()->ValidateServerRequest(cq(),
                                          static_cast<void*>(call_info.tag),
                                          nullptr, nullptr) == GRPC_CALL_OK);
    RequestedCall* rc = new RequestedCall(
        static_cast<void*>(call_info.tag), call_info.cq, call_info.call,
        call_info.initial_metadata, call_info.details);
    return Immediate(MatchResult(server(), cq_idx(), rc));
  }

 private:
  std::function<BatchCallAllocation()> allocator_;
};

// An allocating request matcher for registered methods.
class Server::AllocatingRequestMatcherRegistered
    : public AllocatingRequestMatcherBase {
 public:
  AllocatingRequestMatcherRegistered(
      Server* server, grpc_completion_queue* cq, RegisteredMethod* rm,
      std::function<RegisteredCallAllocation()> allocator)
      : AllocatingRequestMatcherBase(server, cq),
        registered_method_(rm),
        allocator_(std::move(allocator)) {}

  void MatchOrQueue(size_t /*start_request_queue_index*/,
                    CallData* calld) override {
    auto cleanup_ref =
        absl::MakeCleanup([this] { server()->ShutdownUnrefOnRequest(); });
    if (server()->ShutdownRefOnRequest()) {
      RegisteredCallAllocation call_info = allocator_();
      CHECK(server()->ValidateServerRequest(
                cq(), call_info.tag, call_info.optional_payload,
                registered_method_) == GRPC_CALL_OK);
      RequestedCall* rc =
          new RequestedCall(call_info.tag, call_info.cq, call_info.call,
                            call_info.initial_metadata, registered_method_,
                            call_info.deadline, call_info.optional_payload);
      calld->SetState(CallData::CallState::ACTIVATED);
      calld->Publish(cq_idx(), rc);
    } else {
      calld->FailCallCreation();
    }
  }

  ArenaPromise<absl::StatusOr<MatchResult>> MatchRequest(
      size_t /*start_request_queue_index*/) override {
    RegisteredCallAllocation call_info = allocator_();
    CHECK(server()->ValidateServerRequest(cq(), call_info.tag,
                                          call_info.optional_payload,
                                          registered_method_) == GRPC_CALL_OK);
    RequestedCall* rc = new RequestedCall(
        call_info.tag, call_info.cq, call_info.call, call_info.initial_metadata,
        registered_method_, call_info.deadline, call_info.optional_payload);
    return Immediate(MatchResult(server(), cq_idx(), rc));
  }

 private:
  RegisteredMethod* const registered_method_;
  std::function<RegisteredCallAllocation()> allocator_;
};

//
// ChannelBroadcaster
//

namespace {

class ChannelBroadcaster {
 public:
  // This can have an empty constructor and destructor since we want to control
  // when the actual setup and shutdown broadcast take place.

  // Copies over the channels from the locked server.
  void FillChannelsLocked(std::vector<RefCountedPtr<Channel>> channels) {
    DCHECK(channels_.empty());
    channels_ = std::move(channels);
  }

  // Broadcasts a shutdown on each channel.
  void BroadcastShutdown(bool send_goaway, grpc_error_handle force_disconnect) {
    for (const RefCountedPtr<Channel>& channel : channels_) {
      SendShutdown(channel.get(), send_goaway, force_disconnect);
    }
    channels_.clear();  // just for safety against double broadcast
  }

 private:
  struct ShutdownCleanupArgs {
    grpc_closure closure;
    grpc_slice slice;
  };

  static void ShutdownCleanup(void* arg, grpc_error_handle /*error*/) {
    ShutdownCleanupArgs* a = static_cast<ShutdownCleanupArgs*>(arg);
    CSliceUnref(a->slice);
    delete a;
  }

  static void SendShutdown(Channel* channel, bool send_goaway,
                           grpc_error_handle send_disconnect) {
    ShutdownCleanupArgs* sc = new ShutdownCleanupArgs;
    GRPC_CLOSURE_INIT(&sc->closure, ShutdownCleanup, sc,
                      grpc_schedule_on_exec_ctx);
    grpc_transport_op* op = grpc_make_transport_op(&sc->closure);
    grpc_channel_element* elem;
    op->goaway_error =
        send_goaway
            ? grpc_error_set_int(GRPC_ERROR_CREATE("Server shutdown"),
                                 StatusIntProperty::kRpcStatus, GRPC_STATUS_OK)
            : absl::OkStatus();
    sc->slice = grpc_slice_from_copied_string("Server shutdown");
    op->disconnect_with_error = send_disconnect;
    elem = grpc_channel_stack_element(channel->channel_stack(), 0);
    elem->filter->start_transport_op(elem, op);
  }

  std::vector<RefCountedPtr<Channel>> channels_;
};

}  // namespace

//
// Server::TransportConnectivityWatcher
//

class Server::TransportConnectivityWatcher
    : public AsyncConnectivityStateWatcherInterface {
 public:
  TransportConnectivityWatcher(RefCountedPtr<ServerTransport> transport,
                               RefCountedPtr<Server> server)
      : transport_(std::move(transport)), server_(std::move(server)) {}

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& /*status*/) override {
    // Don't do anything until we are being shut down.
    if (new_state != GRPC_CHANNEL_SHUTDOWN) return;
    // Shut down channel.
    MutexLock lock(&server_->mu_global_);
    server_->connections_.erase(transport_.get());
    --server_->connections_open_;
    server_->MaybeFinishShutdown();
  }

  RefCountedPtr<ServerTransport> transport_;
  RefCountedPtr<Server> server_;
};

//
// Server
//

const grpc_channel_filter Server::kServerTopFilter = {
    Server::CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(Server::CallData),
    Server::CallData::InitCallElement,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    Server::CallData::DestroyCallElement,
    sizeof(Server::ChannelData),
    Server::ChannelData::InitChannelElement,
    grpc_channel_stack_no_post_init,
    Server::ChannelData::DestroyChannelElement,
    grpc_channel_next_get_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("server"),
};

namespace {

RefCountedPtr<channelz::ServerNode> CreateChannelzNode(
    const ChannelArgs& args) {
  RefCountedPtr<channelz::ServerNode> channelz_node;
  if (args.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
          .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT)) {
    size_t channel_tracer_max_memory = std::max(
        0, args.GetInt(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE)
               .value_or(GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT));
    channelz_node =
        MakeRefCounted<channelz::ServerNode>(channel_tracer_max_memory);
    channelz_node->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("Server created"));
  }
  return channelz_node;
}

absl::StatusOr<ClientMetadataHandle> CheckClientMetadata(
    ValueOrFailure<ClientMetadataHandle> md) {
  if (!md.ok()) {
    return absl::InternalError("Error reading metadata");
  }
  if (!md.value()->get_pointer(HttpPathMetadata())) {
    return absl::InternalError("Missing :path header");
  }
  if (!md.value()->get_pointer(HttpAuthorityMetadata())) {
    return absl::InternalError("Missing :authority header");
  }
  return std::move(*md);
}
}  // namespace

auto Server::MatchAndPublishCall(CallHandler call_handler) {
  call_handler.SpawnGuarded("request_matcher", [this, call_handler]() mutable {
    return TrySeq(
        // Wait for initial metadata to pass through all filters
        Map(call_handler.PullClientInitialMetadata(), CheckClientMetadata),
        // Match request with requested call
        [this, call_handler](ClientMetadataHandle md) mutable {
          auto* registered_method = static_cast<RegisteredMethod*>(
              md->get(GrpcRegisteredMethod()).value_or(nullptr));
          RequestMatcherInterface* rm;
          grpc_server_register_method_payload_handling payload_handling =
              GRPC_SRM_PAYLOAD_NONE;
          if (registered_method == nullptr) {
            rm = unregistered_request_matcher_.get();
          } else {
            payload_handling = registered_method->payload_handling;
            rm = registered_method->matcher.get();
          }
          auto maybe_read_first_message = If(
              payload_handling == GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER,
              [call_handler]() mutable { return call_handler.PullMessage(); },
              []() -> ValueOrFailure<absl::optional<MessageHandle>> {
                return ValueOrFailure<absl::optional<MessageHandle>>(
                    absl::nullopt);
              });
          return TryJoin<absl::StatusOr>(
              std::move(maybe_read_first_message), rm->MatchRequest(0),
              [md = std::move(md)]() mutable {
                return ValueOrFailure<ClientMetadataHandle>(std::move(md));
              });
        },
        // Publish call to cq
        [call_handler, this](std::tuple<absl::optional<MessageHandle>,
                                        RequestMatcherInterface::MatchResult,
                                        ClientMetadataHandle>
                                 r) {
          RequestMatcherInterface::MatchResult& mr = std::get<1>(r);
          auto md = std::move(std::get<2>(r));
          auto* rc = mr.TakeCall();
          rc->Complete(std::move(std::get<0>(r)), *md);
          grpc_call* call =
              MakeServerCall(call_handler, std::move(md), this,
                             rc->cq_bound_to_call, rc->initial_metadata);
          *rc->call = call;
          return Map(WaitForCqEndOp(false, rc->tag, absl::OkStatus(), mr.cq()),
                     [rc = std::unique_ptr<RequestedCall>(rc)](Empty) {
                       return absl::OkStatus();
                     });
        });
  });
}

absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>
Server::MakeCallDestination(const ChannelArgs& args) {
  InterceptionChainBuilder builder(args);
  builder.AddOnClientInitialMetadata(
      [this](ClientMetadata& md) { SetRegisteredMethodOnMetadata(md); });
  CoreConfiguration::Get().channel_init().AddToInterceptionChainBuilder(
      GRPC_SERVER_CHANNEL, builder);
  return builder.Build(
      MakeCallDestinationFromHandlerFunction([this](CallHandler handler) {
        return MatchAndPublishCall(std::move(handler));
      }));
}

Server::Server(const ChannelArgs& args)
    : channel_args_(args),
      channelz_node_(CreateChannelzNode(args)),
      server_call_tracer_factory_(ServerCallTracerFactory::Get(args)),
      compression_options_(CompressionOptionsFromChannelArgs(args)),
      max_time_in_pending_queue_(Duration::Seconds(
          channel_args_
              .GetInt(GRPC_ARG_SERVER_MAX_UNREQUESTED_TIME_IN_SERVER_SECONDS)
              .value_or(30))) {}

Server::~Server() {
  // Remove the cq pollsets from the config_fetcher.
  if (started_ && config_fetcher_ != nullptr &&
      config_fetcher_->interested_parties() != nullptr) {
    for (grpc_pollset* pollset : pollsets_) {
      grpc_pollset_set_del_pollset(config_fetcher_->interested_parties(),
                                   pollset);
    }
  }
  for (size_t i = 0; i < cqs_.size(); i++) {
    GRPC_CQ_INTERNAL_UNREF(cqs_[i], "server");
  }
}

void Server::AddListener(OrphanablePtr<ListenerInterface> listener) {
  channelz::ListenSocketNode* listen_socket_node =
      listener->channelz_listen_socket_node();
  if (listen_socket_node != nullptr && channelz_node_ != nullptr) {
    channelz_node_->AddChildListenSocket(
        listen_socket_node->RefAsSubclass<channelz::ListenSocketNode>());
  }
  listeners_.emplace_back(std::move(listener));
}

void Server::Start() {
  started_ = true;
  for (grpc_completion_queue* cq : cqs_) {
    if (grpc_cq_can_listen(cq)) {
      pollsets_.push_back(grpc_cq_pollset(cq));
    }
  }
  if (unregistered_request_matcher_ == nullptr) {
    unregistered_request_matcher_ = std::make_unique<RealRequestMatcher>(this);
  }
  for (auto& rm : registered_methods_) {
    if (rm.second->matcher == nullptr) {
      rm.second->matcher = std::make_unique<RealRequestMatcher>(this);
    }
  }
  {
    MutexLock lock(&mu_global_);
    starting_ = true;
  }
  // Register the interested parties from the config fetcher to the cq pollsets
  // before starting listeners so that config fetcher is being polled when the
  // listeners start watch the fetcher.
  if (config_fetcher_ != nullptr &&
      config_fetcher_->interested_parties() != nullptr) {
    for (grpc_pollset* pollset : pollsets_) {
      grpc_pollset_set_add_pollset(config_fetcher_->interested_parties(),
                                   pollset);
    }
  }
  for (auto& listener : listeners_) {
    listener.listener->Start(this, &pollsets_);
  }
  MutexLock lock(&mu_global_);
  starting_ = false;
  starting_cv_.Signal();
}

grpc_error_handle Server::SetupTransport(
    Transport* transport, grpc_pollset* accepting_pollset,
    const ChannelArgs& args,
    const RefCountedPtr<channelz::SocketNode>& socket_node) {
  // Create channel.
  global_stats().IncrementServerChannelsCreated();
  // Set up channelz node.
  if (transport->server_transport() != nullptr) {
    // Take ownership
    // TODO(ctiller): post-v3-transition make this method take an
    // OrphanablePtr<ServerTransport> directly.
    OrphanablePtr<ServerTransport> t(transport->server_transport());
    auto destination = MakeCallDestination(args.SetObject(transport));
    if (!destination.ok()) {
      return absl_status_to_grpc_error(destination.status());
    }
    // TODO(ctiller): add channelz node
    t->SetCallDestination(std::move(*destination));
    MutexLock lock(&mu_global_);
    if (ShutdownCalled()) {
      t->DisconnectWithError(GRPC_ERROR_CREATE("Server shutdown"));
    }
    t->StartConnectivityWatch(MakeOrphanable<TransportConnectivityWatcher>(
        t->RefAsSubclass<ServerTransport>(), Ref()));
    GRPC_TRACE_LOG(server_channel, INFO) << "Adding connection";
    connections_.emplace(std::move(t));
    ++connections_open_;
  } else {
    CHECK(transport->filter_stack_transport() != nullptr);
    absl::StatusOr<RefCountedPtr<Channel>> channel = LegacyChannel::Create(
        "", args.SetObject(transport), GRPC_SERVER_CHANNEL);
    if (!channel.ok()) {
      return absl_status_to_grpc_error(channel.status());
    }
    CHECK(*channel != nullptr);
    auto* channel_stack = (*channel)->channel_stack();
    CHECK(channel_stack != nullptr);
    ChannelData* chand = static_cast<ChannelData*>(
        grpc_channel_stack_element(channel_stack, 0)->channel_data);
    // Set up CQs.
    size_t cq_idx;
    for (cq_idx = 0; cq_idx < cqs_.size(); cq_idx++) {
      if (grpc_cq_pollset(cqs_[cq_idx]) == accepting_pollset) break;
    }
    if (cq_idx == cqs_.size()) {
      // Completion queue not found.  Pick a random one to publish new calls to.
      cq_idx = static_cast<size_t>(rand()) % std::max<size_t>(1, cqs_.size());
    }
    intptr_t channelz_socket_uuid = 0;
    if (socket_node != nullptr) {
      channelz_socket_uuid = socket_node->uuid();
      channelz_node_->AddChildSocket(socket_node);
    }
    // Initialize chand.
    chand->InitTransport(Ref(), std::move(*channel), cq_idx, transport,
                         channelz_socket_uuid);
  }
  return absl::OkStatus();
}

bool Server::HasOpenConnections() {
  MutexLock lock(&mu_global_);
  return !channels_.empty() || !connections_.empty();
}

void Server::SetRegisteredMethodAllocator(
    grpc_completion_queue* cq, void* method_tag,
    std::function<RegisteredCallAllocation()> allocator) {
  RegisteredMethod* rm = static_cast<RegisteredMethod*>(method_tag);
  rm->matcher = std::make_unique<AllocatingRequestMatcherRegistered>(
      this, cq, rm, std::move(allocator));
}

void Server::SetBatchMethodAllocator(
    grpc_completion_queue* cq, std::function<BatchCallAllocation()> allocator) {
  DCHECK(unregistered_request_matcher_ == nullptr);
  unregistered_request_matcher_ =
      std::make_unique<AllocatingRequestMatcherBatch>(this, cq,
                                                      std::move(allocator));
}

void Server::RegisterCompletionQueue(grpc_completion_queue* cq) {
  for (grpc_completion_queue* queue : cqs_) {
    if (queue == cq) return;
  }
  GRPC_CQ_INTERNAL_REF(cq, "server");
  cqs_.push_back(cq);
}

Server::RegisteredMethod* Server::RegisterMethod(
    const char* method, const char* host,
    grpc_server_register_method_payload_handling payload_handling,
    uint32_t flags) {
  if (started_) {
    Crash("Attempting to register method after server started");
  }

  if (!method) {
    LOG(ERROR) << "grpc_server_register_method method string cannot be NULL";
    return nullptr;
  }
  auto key = std::make_pair(host ? host : "", method);
  if (registered_methods_.find(key) != registered_methods_.end()) {
    LOG(ERROR) << "duplicate registration for " << method << "@"
               << (host ? host : "*");
    return nullptr;
  }
  if (flags != 0) {
    LOG(ERROR) << "grpc_server_register_method invalid flags "
               << absl::StrFormat("0x%08x", flags);
    return nullptr;
  }
  auto it = registered_methods_.emplace(
      key, std::make_unique<RegisteredMethod>(method, host, payload_handling,
                                              flags));
  return it.first->second.get();
}

void Server::DoneRequestEvent(void* req, grpc_cq_completion* /*c*/) {
  delete static_cast<RequestedCall*>(req);
}

void Server::FailCall(size_t cq_idx, RequestedCall* rc,
                      grpc_error_handle error) {
  *rc->call = nullptr;
  rc->initial_metadata->count = 0;
  CHECK(!error.ok());
  grpc_cq_end_op(cqs_[cq_idx], rc->tag, error, DoneRequestEvent, rc,
                 &rc->completion);
}

// Before calling MaybeFinishShutdown(), we must hold mu_global_ and not
// hold mu_call_.
void Server::MaybeFinishShutdown() {
  if (!ShutdownReady() || shutdown_published_) {
    return;
  }
  {
    MutexLock lock(&mu_call_);
    KillPendingWorkLocked(GRPC_ERROR_CREATE("Server Shutdown"));
  }
  if (!channels_.empty() || connections_open_ > 0 ||
      listeners_destroyed_ < listeners_.size()) {
    if (gpr_time_cmp(gpr_time_sub(gpr_now(GPR_CLOCK_REALTIME),
                                  last_shutdown_message_time_),
                     gpr_time_from_seconds(1, GPR_TIMESPAN)) >= 0) {
      last_shutdown_message_time_ = gpr_now(GPR_CLOCK_REALTIME);
      VLOG(2) << "Waiting for " << channels_.size() << " channels "
              << connections_open_ << " connections and "
              << listeners_.size() - listeners_destroyed_ << "/"
              << listeners_.size()
              << " listeners to be destroyed before shutting down server";
    }
    return;
  }
  shutdown_published_ = true;
  for (auto& shutdown_tag : shutdown_tags_) {
    Ref().release();
    grpc_cq_end_op(shutdown_tag.cq, shutdown_tag.tag, absl::OkStatus(),
                   DoneShutdownEvent, this, &shutdown_tag.completion);
  }
}

void Server::KillPendingWorkLocked(grpc_error_handle error) {
  if (started_) {
    unregistered_request_matcher_->KillRequests(error);
    unregistered_request_matcher_->ZombifyPending();
    for (auto& rm : registered_methods_) {
      rm.second->matcher->KillRequests(error);
      rm.second->matcher->ZombifyPending();
    }
  }
}

std::vector<RefCountedPtr<Channel>> Server::GetChannelsLocked() const {
  std::vector<RefCountedPtr<Channel>> channels;
  channels.reserve(channels_.size());
  for (const ChannelData* chand : channels_) {
    channels.push_back(chand->channel()->RefAsSubclass<Channel>());
  }
  return channels;
}

void Server::ListenerDestroyDone(void* arg, grpc_error_handle /*error*/) {
  Server* server = static_cast<Server*>(arg);
  MutexLock lock(&server->mu_global_);
  server->listeners_destroyed_++;
  server->MaybeFinishShutdown();
}

namespace {

void DonePublishedShutdown(void* /*done_arg*/, grpc_cq_completion* storage) {
  delete storage;
}

}  // namespace

// - Kills all pending requests-for-incoming-RPC-calls (i.e., the requests made
//   via grpc_server_request_call() and grpc_server_request_registered_call()
//   will now be cancelled). See KillPendingWorkLocked().
//
// - Shuts down the listeners (i.e., the server will no longer listen on the
//   port for new incoming channels).
//
// - Iterates through all channels on the server and sends shutdown msg (see
//   ChannelBroadcaster::BroadcastShutdown() for details) to the clients via
//   the transport layer. The transport layer then guarantees the following:
//    -- Sends shutdown to the client (e.g., HTTP2 transport sends GOAWAY).
//    -- If the server has outstanding calls that are in the process, the
//       connection is NOT closed until the server is done with all those calls.
//    -- Once there are no more calls in progress, the channel is closed.
void Server::ShutdownAndNotify(grpc_completion_queue* cq, void* tag) {
  ChannelBroadcaster broadcaster;
  absl::flat_hash_set<OrphanablePtr<ServerTransport>> removing_connections;
  {
    // Wait for startup to be finished.  Locks mu_global.
    MutexLock lock(&mu_global_);
    while (starting_) {
      starting_cv_.Wait(&mu_global_);
    }
    // Stay locked, and gather up some stuff to do.
    CHECK(grpc_cq_begin_op(cq, tag));
    if (shutdown_published_) {
      grpc_cq_end_op(cq, tag, absl::OkStatus(), DonePublishedShutdown, nullptr,
                     new grpc_cq_completion);
      return;
    }
    shutdown_tags_.emplace_back(tag, cq);
    if (ShutdownCalled()) {
      return;
    }
    last_shutdown_message_time_ = gpr_now(GPR_CLOCK_REALTIME);
    broadcaster.FillChannelsLocked(GetChannelsLocked());
    removing_connections.swap(connections_);
    // Collect all unregistered then registered calls.
    {
      MutexLock lock(&mu_call_);
      KillPendingWorkLocked(GRPC_ERROR_CREATE("Server Shutdown"));
    }
    ShutdownUnrefOnShutdownCall();
  }
  StopListening();
  broadcaster.BroadcastShutdown(/*send_goaway=*/true, absl::OkStatus());
}

void Server::StopListening() {
  for (auto& listener : listeners_) {
    if (listener.listener == nullptr) continue;
    channelz::ListenSocketNode* channelz_listen_socket_node =
        listener.listener->channelz_listen_socket_node();
    if (channelz_node_ != nullptr && channelz_listen_socket_node != nullptr) {
      channelz_node_->RemoveChildListenSocket(
          channelz_listen_socket_node->uuid());
    }
    GRPC_CLOSURE_INIT(&listener.destroy_done, ListenerDestroyDone, this,
                      grpc_schedule_on_exec_ctx);
    listener.listener->SetOnDestroyDone(&listener.destroy_done);
    listener.listener.reset();
  }
}

void Server::CancelAllCalls() {
  ChannelBroadcaster broadcaster;
  {
    MutexLock lock(&mu_global_);
    broadcaster.FillChannelsLocked(GetChannelsLocked());
  }
  broadcaster.BroadcastShutdown(
      /*send_goaway=*/false, GRPC_ERROR_CREATE("Cancelling all calls"));
}

void Server::SendGoaways() {
  ChannelBroadcaster broadcaster;
  {
    MutexLock lock(&mu_global_);
    broadcaster.FillChannelsLocked(GetChannelsLocked());
  }
  broadcaster.BroadcastShutdown(/*send_goaway=*/true, absl::OkStatus());
}

void Server::Orphan() {
  {
    MutexLock lock(&mu_global_);
    CHECK(ShutdownCalled() || listeners_.empty());
    CHECK(listeners_destroyed_ == listeners_.size());
  }
  Unref();
}

grpc_call_error Server::ValidateServerRequest(
    grpc_completion_queue* cq_for_notification, void* tag,
    grpc_byte_buffer** optional_payload, RegisteredMethod* rm) {
  if ((rm == nullptr && optional_payload != nullptr) ||
      ((rm != nullptr) && ((optional_payload == nullptr) !=
                           (rm->payload_handling == GRPC_SRM_PAYLOAD_NONE)))) {
    return GRPC_CALL_ERROR_PAYLOAD_TYPE_MISMATCH;
  }
  if (!grpc_cq_begin_op(cq_for_notification, tag)) {
    return GRPC_CALL_ERROR_COMPLETION_QUEUE_SHUTDOWN;
  }
  return GRPC_CALL_OK;
}

grpc_call_error Server::ValidateServerRequestAndCq(
    size_t* cq_idx, grpc_completion_queue* cq_for_notification, void* tag,
    grpc_byte_buffer** optional_payload, RegisteredMethod* rm) {
  size_t idx;
  for (idx = 0; idx < cqs_.size(); idx++) {
    if (cqs_[idx] == cq_for_notification) {
      break;
    }
  }
  if (idx == cqs_.size()) {
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

grpc_call_error Server::QueueRequestedCall(size_t cq_idx, RequestedCall* rc) {
  if (ShutdownCalled()) {
    FailCall(cq_idx, rc, GRPC_ERROR_CREATE("Server Shutdown"));
    return GRPC_CALL_OK;
  }
  RequestMatcherInterface* rm;
  switch (rc->type) {
    case RequestedCall::Type::BATCH_CALL:
      rm = unregistered_request_matcher_.get();
      break;
    case RequestedCall::Type::REGISTERED_CALL:
      rm = rc->data.registered.method->matcher.get();
      break;
  }
  rm->RequestCallWithPossiblePublish(cq_idx, rc);
  return GRPC_CALL_OK;
}

grpc_call_error Server::RequestCall(grpc_call** call,
                                    grpc_call_details* details,
                                    grpc_metadata_array* request_metadata,
                                    grpc_completion_queue* cq_bound_to_call,
                                    grpc_completion_queue* cq_for_notification,
                                    void* tag) {
  size_t cq_idx;
  grpc_call_error error = ValidateServerRequestAndCq(
      &cq_idx, cq_for_notification, tag, nullptr, nullptr);
  if (error != GRPC_CALL_OK) {
    return error;
  }
  RequestedCall* rc =
      new RequestedCall(tag, cq_bound_to_call, call, request_metadata, details);
  return QueueRequestedCall(cq_idx, rc);
}

grpc_call_error Server::RequestRegisteredCall(
    RegisteredMethod* rm, grpc_call** call, gpr_timespec* deadline,
    grpc_metadata_array* request_metadata, grpc_byte_buffer** optional_payload,
    grpc_completion_queue* cq_bound_to_call,
    grpc_completion_queue* cq_for_notification, void* tag_new) {
  size_t cq_idx;
  grpc_call_error error = ValidateServerRequestAndCq(
      &cq_idx, cq_for_notification, tag_new, optional_payload, rm);
  if (error != GRPC_CALL_OK) {
    return error;
  }
  RequestedCall* rc =
      new RequestedCall(tag_new, cq_bound_to_call, call, request_metadata, rm,
                        deadline, optional_payload);
  return QueueRequestedCall(cq_idx, rc);
}

//
// Server::ChannelData::ConnectivityWatcher
//

class Server::ChannelData::ConnectivityWatcher
    : public AsyncConnectivityStateWatcherInterface {
 public:
  explicit ConnectivityWatcher(ChannelData* chand)
      : chand_(chand), channel_(chand_->channel_->RefAsSubclass<Channel>()) {}

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& /*status*/) override {
    // Don't do anything until we are being shut down.
    if (new_state != GRPC_CHANNEL_SHUTDOWN) return;
    // Shut down channel.
    MutexLock lock(&chand_->server_->mu_global_);
    chand_->Destroy();
  }

  ChannelData* const chand_;
  const RefCountedPtr<Channel> channel_;
};

//
// Server::ChannelData
//

Server::ChannelData::~ChannelData() {
  if (server_ != nullptr) {
    if (server_->channelz_node_ != nullptr && channelz_socket_uuid_ != 0) {
      server_->channelz_node_->RemoveChildSocket(channelz_socket_uuid_);
    }
    {
      MutexLock lock(&server_->mu_global_);
      if (list_position_.has_value()) {
        server_->channels_.erase(*list_position_);
        list_position_.reset();
      }
      server_->MaybeFinishShutdown();
    }
  }
}

void Server::ChannelData::InitTransport(RefCountedPtr<Server> server,
                                        RefCountedPtr<Channel> channel,
                                        size_t cq_idx, Transport* transport,
                                        intptr_t channelz_socket_uuid) {
  server_ = std::move(server);
  channel_ = std::move(channel);
  cq_idx_ = cq_idx;
  channelz_socket_uuid_ = channelz_socket_uuid;
  // Publish channel.
  {
    MutexLock lock(&server_->mu_global_);
    server_->channels_.push_front(this);
    list_position_ = server_->channels_.begin();
  }
  // Start accept_stream transport op.
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  CHECK(transport->filter_stack_transport() != nullptr);
  op->set_accept_stream = true;
  op->set_accept_stream_fn = AcceptStream;
  op->set_registered_method_matcher_fn = [](void* arg,
                                            ClientMetadata* metadata) {
    static_cast<ChannelData*>(arg)->server_->SetRegisteredMethodOnMetadata(
        *metadata);
  };
  op->set_accept_stream_user_data = this;
  op->start_connectivity_watch = MakeOrphanable<ConnectivityWatcher>(this);
  if (server_->ShutdownCalled()) {
    op->disconnect_with_error = GRPC_ERROR_CREATE("Server shutdown");
  }
  transport->PerformOp(op);
}

Server::RegisteredMethod* Server::GetRegisteredMethod(
    const absl::string_view& host, const absl::string_view& path) {
  if (registered_methods_.empty()) return nullptr;
  // check for an exact match with host
  auto it = registered_methods_.find(std::make_pair(host, path));
  if (it != registered_methods_.end()) {
    return it->second.get();
  }
  // check for wildcard method definition (no host set)
  it = registered_methods_.find(std::make_pair("", path));
  if (it != registered_methods_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void Server::SetRegisteredMethodOnMetadata(ClientMetadata& metadata) {
  auto* authority = metadata.get_pointer(HttpAuthorityMetadata());
  if (authority == nullptr) {
    authority = metadata.get_pointer(HostMetadata());
    if (authority == nullptr) {
      // Authority not being set is an RPC error.
      return;
    }
  }
  auto* path = metadata.get_pointer(HttpPathMetadata());
  if (path == nullptr) {
    // Path not being set would result in an RPC error.
    return;
  }
  RegisteredMethod* method =
      GetRegisteredMethod(authority->as_string_view(), path->as_string_view());
  // insert in metadata
  metadata.Set(GrpcRegisteredMethod(), method);
}

void Server::ChannelData::AcceptStream(void* arg, Transport* /*transport*/,
                                       const void* transport_server_data) {
  auto* chand = static_cast<Server::ChannelData*>(arg);
  // create a call
  grpc_call_create_args args;
  args.channel = chand->channel_->RefAsSubclass<Channel>();
  args.server = chand->server_.get();
  args.parent = nullptr;
  args.propagation_mask = 0;
  args.cq = nullptr;
  args.pollset_set_alternative = nullptr;
  args.server_transport_data = transport_server_data;
  args.send_deadline = Timestamp::InfFuture();
  grpc_call* call;
  grpc_error_handle error = grpc_call_create(&args, &call);
  grpc_call_stack* call_stack = grpc_call_get_call_stack(call);
  CHECK_NE(call_stack, nullptr);
  grpc_call_element* elem = grpc_call_stack_element(call_stack, 0);
  auto* calld = static_cast<Server::CallData*>(elem->call_data);
  if (!error.ok()) {
    calld->FailCallCreation();
    return;
  }
  calld->Start(elem);
}

void Server::ChannelData::FinishDestroy(void* arg,
                                        grpc_error_handle /*error*/) {
  auto* chand = static_cast<Server::ChannelData*>(arg);
  Server* server = chand->server_.get();
  auto* channel_stack = chand->channel_->channel_stack();
  chand->channel_.reset();
  server->Unref();
  GRPC_CHANNEL_STACK_UNREF(channel_stack, "Server::ChannelData::Destroy");
}

void Server::ChannelData::Destroy() {
  if (!list_position_.has_value()) return;
  CHECK(server_ != nullptr);
  server_->channels_.erase(*list_position_);
  list_position_.reset();
  server_->Ref().release();
  server_->MaybeFinishShutdown();
  // Unreffed by FinishDestroy
  GRPC_CHANNEL_STACK_REF(channel_->channel_stack(),
                         "Server::ChannelData::Destroy");
  GRPC_CLOSURE_INIT(&finish_destroy_channel_closure_, FinishDestroy, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_TRACE_LOG(server_channel, INFO) << "Disconnected client";
  grpc_transport_op* op =
      grpc_make_transport_op(&finish_destroy_channel_closure_);
  op->set_accept_stream = true;
  grpc_channel_next_op(grpc_channel_stack_element(channel_->channel_stack(), 0),
                       op);
}

grpc_error_handle Server::ChannelData::InitChannelElement(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  CHECK(args->is_first);
  CHECK(!args->is_last);
  new (elem->channel_data) ChannelData();
  return absl::OkStatus();
}

void Server::ChannelData::DestroyChannelElement(grpc_channel_element* elem) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  chand->~ChannelData();
}

//
// Server::CallData
//

Server::CallData::CallData(grpc_call_element* elem,
                           const grpc_call_element_args& args,
                           RefCountedPtr<Server> server)
    : server_(std::move(server)),
      call_(grpc_call_from_top_element(elem)),
      call_combiner_(args.call_combiner) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
}

Server::CallData::~CallData() {
  CHECK(state_.load(std::memory_order_relaxed) != CallState::PENDING);
  grpc_metadata_array_destroy(&initial_metadata_);
  grpc_byte_buffer_destroy(payload_);
}

void Server::CallData::SetState(CallState state) {
  state_.store(state, std::memory_order_relaxed);
}

bool Server::CallData::MaybeActivate() {
  CallState expected = CallState::PENDING;
  return state_.compare_exchange_strong(expected, CallState::ACTIVATED,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed);
}

void Server::CallData::FailCallCreation() {
  CallState expected_not_started = CallState::NOT_STARTED;
  CallState expected_pending = CallState::PENDING;
  if (state_.compare_exchange_strong(expected_not_started, CallState::ZOMBIED,
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire)) {
    KillZombie();
  } else if (state_.compare_exchange_strong(
                 expected_pending, CallState::ZOMBIED,
                 std::memory_order_acq_rel, std::memory_order_relaxed)) {
    // Zombied call will be destroyed when it's removed from the pending
    // queue... later.
  }
}

void Server::CallData::Start(grpc_call_element* elem) {
  grpc_op op;
  op.op = GRPC_OP_RECV_INITIAL_METADATA;
  op.flags = 0;
  op.reserved = nullptr;
  op.data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_;
  GRPC_CLOSURE_INIT(&recv_initial_metadata_batch_complete_,
                    RecvInitialMetadataBatchComplete, elem,
                    grpc_schedule_on_exec_ctx);
  grpc_call_start_batch_and_execute(call_, &op, 1,
                                    &recv_initial_metadata_batch_complete_);
}

void Server::CallData::Publish(size_t cq_idx, RequestedCall* rc) {
  grpc_call_set_completion_queue(call_, rc->cq_bound_to_call);
  *rc->call = call_;
  cq_new_ = server_->cqs_[cq_idx];
  std::swap(*rc->initial_metadata, initial_metadata_);
  switch (rc->type) {
    case RequestedCall::Type::BATCH_CALL:
      CHECK(host_.has_value());
      CHECK(path_.has_value());
      rc->data.batch.details->host = CSliceRef(host_->c_slice());
      rc->data.batch.details->method = CSliceRef(path_->c_slice());
      rc->data.batch.details->deadline =
          deadline_.as_timespec(GPR_CLOCK_MONOTONIC);
      break;
    case RequestedCall::Type::REGISTERED_CALL:
      *rc->data.registered.deadline =
          deadline_.as_timespec(GPR_CLOCK_MONOTONIC);
      if (rc->data.registered.optional_payload != nullptr) {
        *rc->data.registered.optional_payload = payload_;
        payload_ = nullptr;
      }
      break;
    default:
      GPR_UNREACHABLE_CODE(return);
  }
  grpc_cq_end_op(cq_new_, rc->tag, absl::OkStatus(), Server::DoneRequestEvent,
                 rc, &rc->completion, true);
}

void Server::CallData::PublishNewRpc(void* arg, grpc_error_handle error) {
  grpc_call_element* call_elem = static_cast<grpc_call_element*>(arg);
  auto* calld = static_cast<Server::CallData*>(call_elem->call_data);
  auto* chand = static_cast<Server::ChannelData*>(call_elem->channel_data);
  RequestMatcherInterface* rm = calld->matcher_;
  Server* server = rm->server();
  if (!error.ok() || server->ShutdownCalled()) {
    calld->state_.store(CallState::ZOMBIED, std::memory_order_relaxed);
    calld->KillZombie();
    return;
  }
  rm->MatchOrQueue(chand->cq_idx(), calld);
}

namespace {

void KillZombieClosure(void* call, grpc_error_handle /*error*/) {
  grpc_call_unref(static_cast<grpc_call*>(call));
}

}  // namespace

void Server::CallData::KillZombie() {
  GRPC_CLOSURE_INIT(&kill_zombie_closure_, KillZombieClosure, call_,
                    grpc_schedule_on_exec_ctx);
  ExecCtx::Run(DEBUG_LOCATION, &kill_zombie_closure_, absl::OkStatus());
}

// If this changes, change MakeCallPromise too.
void Server::CallData::StartNewRpc(grpc_call_element* elem) {
  if (server_->ShutdownCalled()) {
    state_.store(CallState::ZOMBIED, std::memory_order_relaxed);
    KillZombie();
    return;
  }
  // Find request matcher.
  matcher_ = server_->unregistered_request_matcher_.get();
  grpc_server_register_method_payload_handling payload_handling =
      GRPC_SRM_PAYLOAD_NONE;
  if (path_.has_value() && host_.has_value()) {
    RegisteredMethod* rm = static_cast<RegisteredMethod*>(
        recv_initial_metadata_->get(GrpcRegisteredMethod()).value_or(nullptr));
    if (rm != nullptr) {
      matcher_ = rm->matcher.get();
      payload_handling = rm->payload_handling;
    }
  }
  // Start recv_message op if needed.
  switch (payload_handling) {
    case GRPC_SRM_PAYLOAD_NONE:
      PublishNewRpc(elem, absl::OkStatus());
      break;
    case GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER: {
      grpc_op op;
      op.op = GRPC_OP_RECV_MESSAGE;
      op.flags = 0;
      op.reserved = nullptr;
      op.data.recv_message.recv_message = &payload_;
      GRPC_CLOSURE_INIT(&publish_, PublishNewRpc, elem,
                        grpc_schedule_on_exec_ctx);
      grpc_call_start_batch_and_execute(call_, &op, 1, &publish_);
      break;
    }
  }
}

void Server::CallData::RecvInitialMetadataBatchComplete(
    void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  auto* calld = static_cast<Server::CallData*>(elem->call_data);
  if (!error.ok()) {
    VLOG(2) << "Failed call creation: " << StatusToString(error);
    calld->FailCallCreation();
    return;
  }
  calld->StartNewRpc(elem);
}

void Server::CallData::StartTransportStreamOpBatchImpl(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  if (batch->recv_initial_metadata) {
    recv_initial_metadata_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata;
    original_recv_initial_metadata_ready_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &recv_initial_metadata_ready_;
  }
  if (batch->recv_trailing_metadata) {
    original_recv_trailing_metadata_ready_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }
  grpc_call_next_op(elem, batch);
}

void Server::CallData::RecvInitialMetadataReady(void* arg,
                                                grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (error.ok()) {
    calld->path_ = calld->recv_initial_metadata_->Take(HttpPathMetadata());
    auto* host =
        calld->recv_initial_metadata_->get_pointer(HttpAuthorityMetadata());
    if (host != nullptr) calld->host_.emplace(host->Ref());
  }
  auto op_deadline = calld->recv_initial_metadata_->get(GrpcTimeoutMetadata());
  if (op_deadline.has_value()) {
    calld->deadline_ = *op_deadline;
    Call::FromC(calld->call_)->UpdateDeadline(*op_deadline);
  }
  if (calld->host_.has_value() && calld->path_.has_value()) {
    // do nothing
  } else if (error.ok()) {
    // Pass the error reference to calld->recv_initial_metadata_error
    error = absl::UnknownError("Missing :authority or :path");
    calld->recv_initial_metadata_error_ = error;
  }
  grpc_closure* closure = calld->original_recv_initial_metadata_ready_;
  calld->original_recv_initial_metadata_ready_ = nullptr;
  if (calld->seen_recv_trailing_metadata_ready_) {
    GRPC_CALL_COMBINER_START(calld->call_combiner_,
                             &calld->recv_trailing_metadata_ready_,
                             calld->recv_trailing_metadata_error_,
                             "continue server recv_trailing_metadata_ready");
  }
  Closure::Run(DEBUG_LOCATION, closure, error);
}

void Server::CallData::RecvTrailingMetadataReady(void* arg,
                                                 grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  CallData* calld = static_cast<CallData*>(elem->call_data);
  if (calld->original_recv_initial_metadata_ready_ != nullptr) {
    calld->recv_trailing_metadata_error_ = error;
    calld->seen_recv_trailing_metadata_ready_ = true;
    GRPC_CLOSURE_INIT(&calld->recv_trailing_metadata_ready_,
                      RecvTrailingMetadataReady, elem,
                      grpc_schedule_on_exec_ctx);
    GRPC_CALL_COMBINER_STOP(calld->call_combiner_,
                            "deferring server recv_trailing_metadata_ready "
                            "until after recv_initial_metadata_ready");
    return;
  }
  error = grpc_error_add_child(error, calld->recv_initial_metadata_error_);
  Closure::Run(DEBUG_LOCATION, calld->original_recv_trailing_metadata_ready_,
               error);
}

grpc_error_handle Server::CallData::InitCallElement(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  auto* chand = static_cast<ChannelData*>(elem->channel_data);
  new (elem->call_data) Server::CallData(elem, *args, chand->server());
  return absl::OkStatus();
}

void Server::CallData::DestroyCallElement(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* /*ignored*/) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->~CallData();
}

void Server::CallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  auto* calld = static_cast<CallData*>(elem->call_data);
  calld->StartTransportStreamOpBatchImpl(elem, batch);
}

}  // namespace grpc_core

//
// C-core API
//

grpc_server* grpc_server_create(const grpc_channel_args* args, void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_create(" << args << ", " << reserved << ")";
  grpc_core::Server* server =
      new grpc_core::Server(grpc_core::CoreConfiguration::Get()
                                .channel_args_preconditioning()
                                .PreconditionChannelArgs(args));
  return server->c_ptr();
}

void grpc_server_register_completion_queue(grpc_server* server,
                                           grpc_completion_queue* cq,
                                           void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_register_completion_queue(server=" << server
      << ", cq=" << cq << ", reserved=" << reserved << ")";
  CHECK(!reserved);
  auto cq_type = grpc_get_cq_completion_type(cq);
  if (cq_type != GRPC_CQ_NEXT && cq_type != GRPC_CQ_CALLBACK) {
    LOG(INFO) << "Completion queue of type " << static_cast<int>(cq_type)
              << " is being registered as a server-completion-queue";
    // Ideally we should log an error and abort but ruby-wrapped-language API
    // calls grpc_completion_queue_pluck() on server completion queues
  }
  grpc_core::Server::FromC(server)->RegisterCompletionQueue(cq);
}

void* grpc_server_register_method(
    grpc_server* server, const char* method, const char* host,
    grpc_server_register_method_payload_handling payload_handling,
    uint32_t flags) {
  GRPC_TRACE_LOG(api, INFO) << "grpc_server_register_method(server=" << server
                            << ", method=" << method << ", host=" << host
                            << ", flags=" << absl::StrFormat("0x%08x", flags);
  return grpc_core::Server::FromC(server)->RegisterMethod(
      method, host, payload_handling, flags);
}

void grpc_server_start(grpc_server* server) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO) << "grpc_server_start(server=" << server << ")";
  grpc_core::Server::FromC(server)->Start();
}

void grpc_server_shutdown_and_notify(grpc_server* server,
                                     grpc_completion_queue* cq, void* tag) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_shutdown_and_notify(server=" << server << ", cq=" << cq
      << ", tag=" << tag << ")";
  grpc_core::Server::FromC(server)->ShutdownAndNotify(cq, tag);
}

void grpc_server_cancel_all_calls(grpc_server* server) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_cancel_all_calls(server=" << server << ")";
  grpc_core::Server::FromC(server)->CancelAllCalls();
}

void grpc_server_destroy(grpc_server* server) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO) << "grpc_server_destroy(server=" << server << ")";
  grpc_core::Server::FromC(server)->Orphan();
}

grpc_call_error grpc_server_request_call(
    grpc_server* server, grpc_call** call, grpc_call_details* details,
    grpc_metadata_array* request_metadata,
    grpc_completion_queue* cq_bound_to_call,
    grpc_completion_queue* cq_for_notification, void* tag) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_request_call("
      << "server=" << server << ", call=" << call << ", details=" << details
      << ", initial_metadata=" << request_metadata
      << ", cq_bound_to_call=" << cq_bound_to_call
      << ", cq_for_notification=" << cq_for_notification << ", tag=" << tag;
  return grpc_core::Server::FromC(server)->RequestCall(
      call, details, request_metadata, cq_bound_to_call, cq_for_notification,
      tag);
}

grpc_call_error grpc_server_request_registered_call(
    grpc_server* server, void* registered_method, grpc_call** call,
    gpr_timespec* deadline, grpc_metadata_array* request_metadata,
    grpc_byte_buffer** optional_payload,
    grpc_completion_queue* cq_bound_to_call,
    grpc_completion_queue* cq_for_notification, void* tag_new) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  auto* rm =
      static_cast<grpc_core::Server::RegisteredMethod*>(registered_method);
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_request_registered_call("
      << "server=" << server << ", registered_method=" << registered_method
      << ", call=" << call << ", deadline=" << deadline
      << ", request_metadata=" << request_metadata
      << ", optional_payload=" << optional_payload
      << ", cq_bound_to_call=" << cq_bound_to_call
      << ", cq_for_notification=" << cq_for_notification << ", tag=" << tag_new
      << ")";
  return grpc_core::Server::FromC(server)->RequestRegisteredCall(
      rm, call, deadline, request_metadata, optional_payload, cq_bound_to_call,
      cq_for_notification, tag_new);
}

void grpc_server_set_config_fetcher(
    grpc_server* server, grpc_server_config_fetcher* server_config_fetcher) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_set_config_fetcher(server=" << server
      << ", config_fetcher=" << server_config_fetcher << ")";
  grpc_core::Server::FromC(server)->set_config_fetcher(
      std::unique_ptr<grpc_server_config_fetcher>(server_config_fetcher));
}

void grpc_server_config_fetcher_destroy(
    grpc_server_config_fetcher* server_config_fetcher) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_server_config_fetcher_destroy(config_fetcher="
      << server_config_fetcher << ")";
  delete server_config_fetcher;
}
