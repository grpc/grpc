/*
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

#include <grpcpp/server.h>

#include <cstdlib>
#include <sstream>
#include <type_traits>
#include <utility>

#include <grpc/grpc.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/impl/codegen/async_unary_call.h>
#include <grpcpp/impl/codegen/byte_buffer.h>
#include <grpcpp/impl/codegen/call.h>
#include <grpcpp/impl/codegen/completion_queue_tag.h>
#include <grpcpp/impl/codegen/method_handler.h>
#include <grpcpp/impl/codegen/server_interceptor.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/impl/rpc_service_method.h>
#include <grpcpp/impl/server_initializer.h>
#include <grpcpp/impl/service_type.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/time.h>

#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/cpp/server/external_connection_acceptor_impl.h"
#include "src/cpp/server/health/default_health_check_service.h"
#include "src/cpp/thread_manager/thread_manager.h"

namespace grpc {
namespace {

// The default value for maximum number of threads that can be created in the
// sync server. This value of INT_MAX is chosen to match the default behavior if
// no ResourceQuota is set. To modify the max number of threads in a sync
// server, pass a custom ResourceQuota object  (with the desired number of
// max-threads set) to the server builder.
#define DEFAULT_MAX_SYNC_SERVER_THREADS INT_MAX

// How many callback requests of each method should we pre-register at start
#define DEFAULT_CALLBACK_REQS_PER_METHOD 512

// What is the (soft) limit for outstanding requests in the server
#define SOFT_MAXIMUM_CALLBACK_REQS_OUTSTANDING 30000

// If the number of unmatched requests for a method drops below this amount, try
// to allocate extra unless it pushes the total number of callbacks above the
// soft maximum
#define SOFT_MINIMUM_SPARE_CALLBACK_REQS_PER_METHOD 128

class DefaultGlobalCallbacks final : public Server::GlobalCallbacks {
 public:
  ~DefaultGlobalCallbacks() override {}
  void PreSynchronousRequest(ServerContext* /*context*/) override {}
  void PostSynchronousRequest(ServerContext* /*context*/) override {}
};

std::shared_ptr<Server::GlobalCallbacks> g_callbacks = nullptr;
gpr_once g_once_init_callbacks = GPR_ONCE_INIT;

void InitGlobalCallbacks() {
  if (!g_callbacks) {
    g_callbacks.reset(new DefaultGlobalCallbacks());
  }
}

class ShutdownTag : public internal::CompletionQueueTag {
 public:
  bool FinalizeResult(void** /*tag*/, bool* /*status*/) { return false; }
};

class DummyTag : public internal::CompletionQueueTag {
 public:
  bool FinalizeResult(void** /*tag*/, bool* /*status*/) { return true; }
};

class UnimplementedAsyncRequestContext {
 protected:
  UnimplementedAsyncRequestContext() : generic_stream_(&server_context_) {}

  GenericServerContext server_context_;
  GenericServerAsyncReaderWriter generic_stream_;
};

// TODO(vjpai): Just for this file, use some contents of the experimental
// namespace here to make the code easier to read below. Remove this when
// de-experimentalized fully.
#ifndef GRPC_CALLBACK_API_NONEXPERIMENTAL
using ::grpc::experimental::CallbackGenericService;
using ::grpc::experimental::CallbackServerContext;
using ::grpc::experimental::GenericCallbackServerContext;
#endif

}  // namespace

ServerInterface::BaseAsyncRequest::BaseAsyncRequest(
    ServerInterface* server, ServerContext* context,
    internal::ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    ServerCompletionQueue* notification_cq, void* tag, bool delete_on_finalize)
    : server_(server),
      context_(context),
      stream_(stream),
      call_cq_(call_cq),
      notification_cq_(notification_cq),
      tag_(tag),
      delete_on_finalize_(delete_on_finalize),
      call_(nullptr),
      done_intercepting_(false) {
  /* Set up interception state partially for the receive ops. call_wrapper_ is
   * not filled at this point, but it will be filled before the interceptors are
   * run. */
  interceptor_methods_.SetCall(&call_wrapper_);
  interceptor_methods_.SetReverse();
  call_cq_->RegisterAvalanching();  // This op will trigger more ops
}

ServerInterface::BaseAsyncRequest::~BaseAsyncRequest() {
  call_cq_->CompleteAvalanching();
}

bool ServerInterface::BaseAsyncRequest::FinalizeResult(void** tag,
                                                       bool* status) {
  if (done_intercepting_) {
    *tag = tag_;
    if (delete_on_finalize_) {
      delete this;
    }
    return true;
  }
  context_->set_call(call_);
  context_->cq_ = call_cq_;
  if (call_wrapper_.call() == nullptr) {
    // Fill it since it is empty.
    call_wrapper_ = internal::Call(
        call_, server_, call_cq_, server_->max_receive_message_size(), nullptr);
  }

  // just the pointers inside call are copied here
  stream_->BindCall(&call_wrapper_);

  if (*status && call_ && call_wrapper_.server_rpc_info()) {
    done_intercepting_ = true;
    // Set interception point for RECV INITIAL METADATA
    interceptor_methods_.AddInterceptionHookPoint(
        experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA);
    interceptor_methods_.SetRecvInitialMetadata(&context_->client_metadata_);
    if (interceptor_methods_.RunInterceptors(
            [this]() { ContinueFinalizeResultAfterInterception(); })) {
      // There are no interceptors to run. Continue
    } else {
      // There were interceptors to be run, so
      // ContinueFinalizeResultAfterInterception will be run when interceptors
      // are done.
      return false;
    }
  }
  if (*status && call_) {
    context_->BeginCompletionOp(&call_wrapper_, nullptr, nullptr);
  }
  *tag = tag_;
  if (delete_on_finalize_) {
    delete this;
  }
  return true;
}

void ServerInterface::BaseAsyncRequest::
    ContinueFinalizeResultAfterInterception() {
  context_->BeginCompletionOp(&call_wrapper_, nullptr, nullptr);
  // Queue a tag which will be returned immediately
  grpc_core::ExecCtx exec_ctx;
  grpc_cq_begin_op(notification_cq_->cq(), this);
  grpc_cq_end_op(
      notification_cq_->cq(), this, GRPC_ERROR_NONE,
      [](void* /*arg*/, grpc_cq_completion* completion) { delete completion; },
      nullptr, new grpc_cq_completion());
}

ServerInterface::RegisteredAsyncRequest::RegisteredAsyncRequest(
    ServerInterface* server, ServerContext* context,
    internal::ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    ServerCompletionQueue* notification_cq, void* tag, const char* name,
    internal::RpcMethod::RpcType type)
    : BaseAsyncRequest(server, context, stream, call_cq, notification_cq, tag,
                       true),
      name_(name),
      type_(type) {}

void ServerInterface::RegisteredAsyncRequest::IssueRequest(
    void* registered_method, grpc_byte_buffer** payload,
    ServerCompletionQueue* notification_cq) {
  // The following call_start_batch is internally-generated so no need for an
  // explanatory log on failure.
  GPR_ASSERT(grpc_server_request_registered_call(
                 server_->server(), registered_method, &call_,
                 &context_->deadline_, context_->client_metadata_.arr(),
                 payload, call_cq_->cq(), notification_cq->cq(),
                 this) == GRPC_CALL_OK);
}

ServerInterface::GenericAsyncRequest::GenericAsyncRequest(
    ServerInterface* server, GenericServerContext* context,
    internal::ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    ServerCompletionQueue* notification_cq, void* tag, bool delete_on_finalize)
    : BaseAsyncRequest(server, context, stream, call_cq, notification_cq, tag,
                       delete_on_finalize) {
  grpc_call_details_init(&call_details_);
  GPR_ASSERT(notification_cq);
  GPR_ASSERT(call_cq);
  // The following call_start_batch is internally-generated so no need for an
  // explanatory log on failure.
  GPR_ASSERT(grpc_server_request_call(server->server(), &call_, &call_details_,
                                      context->client_metadata_.arr(),
                                      call_cq->cq(), notification_cq->cq(),
                                      this) == GRPC_CALL_OK);
}

bool ServerInterface::GenericAsyncRequest::FinalizeResult(void** tag,
                                                          bool* status) {
  // If we are done intercepting, there is nothing more for us to do
  if (done_intercepting_) {
    return BaseAsyncRequest::FinalizeResult(tag, status);
  }
  // TODO(yangg) remove the copy here.
  if (*status) {
    static_cast<GenericServerContext*>(context_)->method_ =
        StringFromCopiedSlice(call_details_.method);
    static_cast<GenericServerContext*>(context_)->host_ =
        StringFromCopiedSlice(call_details_.host);
    context_->deadline_ = call_details_.deadline;
  }
  grpc_slice_unref(call_details_.method);
  grpc_slice_unref(call_details_.host);
  call_wrapper_ = internal::Call(
      call_, server_, call_cq_, server_->max_receive_message_size(),
      context_->set_server_rpc_info(
          static_cast<GenericServerContext*>(context_)->method_.c_str(),
          internal::RpcMethod::BIDI_STREAMING,
          *server_->interceptor_creators()));
  return BaseAsyncRequest::FinalizeResult(tag, status);
}

namespace {
class ShutdownCallback : public grpc_experimental_completion_queue_functor {
 public:
  ShutdownCallback() {
    functor_run = &ShutdownCallback::Run;
    // Set inlineable to true since this callback is trivial and thus does not
    // need to be run from the executor (triggering a thread hop). This should
    // only be used by internal callbacks like this and not by user application
    // code.
    inlineable = true;
  }
  // TakeCQ takes ownership of the cq into the shutdown callback
  // so that the shutdown callback will be responsible for destroying it
  void TakeCQ(CompletionQueue* cq) { cq_ = cq; }

  // The Run function will get invoked by the completion queue library
  // when the shutdown is actually complete
  static void Run(grpc_experimental_completion_queue_functor* cb, int) {
    auto* callback = static_cast<ShutdownCallback*>(cb);
    delete callback->cq_;
    delete callback;
  }

 private:
  CompletionQueue* cq_ = nullptr;
};
}  // namespace

}  // namespace grpc

namespace grpc_impl {

/// Use private inheritance rather than composition only to establish order
/// of construction, since the public base class should be constructed after the
/// elements belonging to the private base class are constructed. This is not
/// possible using true composition.
class Server::UnimplementedAsyncRequest final
    : private grpc::UnimplementedAsyncRequestContext,
      public GenericAsyncRequest {
 public:
  UnimplementedAsyncRequest(Server* server, grpc::ServerCompletionQueue* cq)
      : GenericAsyncRequest(server, &server_context_, &generic_stream_, cq, cq,
                            nullptr, false),
        server_(server),
        cq_(cq) {}

  bool FinalizeResult(void** tag, bool* status) override;

  grpc::ServerContext* context() { return &server_context_; }
  grpc::GenericServerAsyncReaderWriter* stream() { return &generic_stream_; }

 private:
  Server* const server_;
  grpc::ServerCompletionQueue* const cq_;
};

/// UnimplementedAsyncResponse should not post user-visible completions to the
/// C++ completion queue, but is generated as a CQ event by the core
class Server::UnimplementedAsyncResponse final
    : public grpc::internal::CallOpSet<
          grpc::internal::CallOpSendInitialMetadata,
          grpc::internal::CallOpServerSendStatus> {
 public:
  UnimplementedAsyncResponse(UnimplementedAsyncRequest* request);
  ~UnimplementedAsyncResponse() { delete request_; }

  bool FinalizeResult(void** tag, bool* status) override {
    if (grpc::internal::CallOpSet<
            grpc::internal::CallOpSendInitialMetadata,
            grpc::internal::CallOpServerSendStatus>::FinalizeResult(tag,
                                                                    status)) {
      delete this;
    } else {
      // The tag was swallowed due to interception. We will see it again.
    }
    return false;
  }

 private:
  UnimplementedAsyncRequest* const request_;
};

class Server::SyncRequest final : public grpc::internal::CompletionQueueTag {
 public:
  SyncRequest(grpc::internal::RpcServiceMethod* method, void* method_tag)
      : method_(method),
        method_tag_(method_tag),
        in_flight_(false),
        has_request_payload_(method->method_type() ==
                                 grpc::internal::RpcMethod::NORMAL_RPC ||
                             method->method_type() ==
                                 grpc::internal::RpcMethod::SERVER_STREAMING),
        call_details_(nullptr),
        cq_(nullptr) {
    grpc_metadata_array_init(&request_metadata_);
  }

  ~SyncRequest() {
    if (call_details_) {
      delete call_details_;
    }
    grpc_metadata_array_destroy(&request_metadata_);
  }

  void SetupRequest() { cq_ = grpc_completion_queue_create_for_pluck(nullptr); }

  void TeardownRequest() {
    grpc_completion_queue_destroy(cq_);
    cq_ = nullptr;
  }

  void Request(grpc_server* server, grpc_completion_queue* notify_cq) {
    GPR_ASSERT(cq_ && !in_flight_);
    in_flight_ = true;
    if (method_tag_) {
      if (grpc_server_request_registered_call(
              server, method_tag_, &call_, &deadline_, &request_metadata_,
              has_request_payload_ ? &request_payload_ : nullptr, cq_,
              notify_cq, this) != GRPC_CALL_OK) {
        TeardownRequest();
        return;
      }
    } else {
      if (!call_details_) {
        call_details_ = new grpc_call_details;
        grpc_call_details_init(call_details_);
      }
      if (grpc_server_request_call(server, &call_, call_details_,
                                   &request_metadata_, cq_, notify_cq,
                                   this) != GRPC_CALL_OK) {
        TeardownRequest();
        return;
      }
    }
  }

  void PostShutdownCleanup() {
    if (call_) {
      grpc_call_unref(call_);
      call_ = nullptr;
    }
    if (cq_) {
      grpc_completion_queue_destroy(cq_);
      cq_ = nullptr;
    }
  }

  bool FinalizeResult(void** /*tag*/, bool* status) override {
    if (!*status) {
      grpc_completion_queue_destroy(cq_);
      cq_ = nullptr;
    }
    if (call_details_) {
      deadline_ = call_details_->deadline;
      grpc_call_details_destroy(call_details_);
      grpc_call_details_init(call_details_);
    }
    return true;
  }

  // The CallData class represents a call that is "active" as opposed
  // to just being requested. It wraps and takes ownership of the cq from
  // the call request
  class CallData final {
   public:
    explicit CallData(Server* server, SyncRequest* mrd)
        : cq_(mrd->cq_),
          ctx_(mrd->deadline_, &mrd->request_metadata_),
          has_request_payload_(mrd->has_request_payload_),
          request_payload_(has_request_payload_ ? mrd->request_payload_
                                                : nullptr),
          request_(nullptr),
          method_(mrd->method_),
          call_(
              mrd->call_, server, &cq_, server->max_receive_message_size(),
              ctx_.set_server_rpc_info(method_->name(), method_->method_type(),
                                       server->interceptor_creators_)),
          server_(server),
          global_callbacks_(nullptr),
          resources_(false) {
      ctx_.set_call(mrd->call_);
      ctx_.cq_ = &cq_;
      GPR_ASSERT(mrd->in_flight_);
      mrd->in_flight_ = false;
      mrd->request_metadata_.count = 0;
    }

    ~CallData() {
      if (has_request_payload_ && request_payload_) {
        grpc_byte_buffer_destroy(request_payload_);
      }
    }

    void Run(const std::shared_ptr<GlobalCallbacks>& global_callbacks,
             bool resources) {
      global_callbacks_ = global_callbacks;
      resources_ = resources;

      interceptor_methods_.SetCall(&call_);
      interceptor_methods_.SetReverse();
      // Set interception point for RECV INITIAL METADATA
      interceptor_methods_.AddInterceptionHookPoint(
          grpc::experimental::InterceptionHookPoints::
              POST_RECV_INITIAL_METADATA);
      interceptor_methods_.SetRecvInitialMetadata(&ctx_.client_metadata_);

      if (has_request_payload_) {
        // Set interception point for RECV MESSAGE
        auto* handler = resources_ ? method_->handler()
                                   : server_->resource_exhausted_handler_.get();
        request_ = handler->Deserialize(call_.call(), request_payload_,
                                        &request_status_, nullptr);

        request_payload_ = nullptr;
        interceptor_methods_.AddInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::POST_RECV_MESSAGE);
        interceptor_methods_.SetRecvMessage(request_, nullptr);
      }

      if (interceptor_methods_.RunInterceptors(
              [this]() { ContinueRunAfterInterception(); })) {
        ContinueRunAfterInterception();
      } else {
        // There were interceptors to be run, so ContinueRunAfterInterception
        // will be run when interceptors are done.
      }
    }

    void ContinueRunAfterInterception() {
      {
        ctx_.BeginCompletionOp(&call_, nullptr, nullptr);
        global_callbacks_->PreSynchronousRequest(&ctx_);
        auto* handler = resources_ ? method_->handler()
                                   : server_->resource_exhausted_handler_.get();
        handler->RunHandler(grpc::internal::MethodHandler::HandlerParameter(
            &call_, &ctx_, request_, request_status_, nullptr, nullptr));
        request_ = nullptr;
        global_callbacks_->PostSynchronousRequest(&ctx_);

        cq_.Shutdown();

        grpc::internal::CompletionQueueTag* op_tag = ctx_.GetCompletionOpTag();
        cq_.TryPluck(op_tag, gpr_inf_future(GPR_CLOCK_REALTIME));

        /* Ensure the cq_ is shutdown */
        grpc::DummyTag ignored_tag;
        GPR_ASSERT(cq_.Pluck(&ignored_tag) == false);
      }
      delete this;
    }

   private:
    grpc::CompletionQueue cq_;
    grpc::ServerContext ctx_;
    const bool has_request_payload_;
    grpc_byte_buffer* request_payload_;
    void* request_;
    grpc::Status request_status_;
    grpc::internal::RpcServiceMethod* const method_;
    grpc::internal::Call call_;
    Server* server_;
    std::shared_ptr<GlobalCallbacks> global_callbacks_;
    bool resources_;
    grpc::internal::InterceptorBatchMethodsImpl interceptor_methods_;
  };

 private:
  grpc::internal::RpcServiceMethod* const method_;
  void* const method_tag_;
  bool in_flight_;
  const bool has_request_payload_;
  grpc_call* call_;
  grpc_call_details* call_details_;
  gpr_timespec deadline_;
  grpc_metadata_array request_metadata_;
  grpc_byte_buffer* request_payload_;
  grpc_completion_queue* cq_;
};

class Server::CallbackRequestBase : public grpc::internal::CompletionQueueTag {
 public:
  virtual ~CallbackRequestBase() {}
  virtual bool Request() = 0;
};

template <class ServerContextType>
class Server::CallbackRequest final : public Server::CallbackRequestBase {
 public:
  static_assert(
      std::is_base_of<grpc::CallbackServerContext, ServerContextType>::value,
      "ServerContextType must be derived from CallbackServerContext");

  // The constructor needs to know the server for this callback request and its
  // index in the server's request count array to allow for proper dynamic
  // requesting of incoming RPCs. For codegen services, the values of method and
  // method_tag represent the defined characteristics of the method being
  // requested. For generic services, method and method_tag are nullptr since
  // these services don't have pre-defined methods or method registration tags.
  CallbackRequest(Server* server, size_t method_idx,
                  grpc::internal::RpcServiceMethod* method, void* method_tag)
      : server_(server),
        method_index_(method_idx),
        method_(method),
        method_tag_(method_tag),
        has_request_payload_(
            method_ != nullptr &&
            (method->method_type() == grpc::internal::RpcMethod::NORMAL_RPC ||
             method->method_type() ==
                 grpc::internal::RpcMethod::SERVER_STREAMING)),
        cq_(server->CallbackCQ()),
        tag_(this) {
    server_->callback_reqs_outstanding_++;
    Setup();
  }

  ~CallbackRequest() {
    Clear();

    // The counter of outstanding requests must be decremented
    // under a lock in case it causes the server shutdown.
    grpc::internal::MutexLock l(&server_->callback_reqs_mu_);
    if (--server_->callback_reqs_outstanding_ == 0) {
      server_->callback_reqs_done_cv_.Signal();
    }
  }

  bool Request() override {
    if (method_tag_) {
      if (grpc_server_request_registered_call(
              server_->c_server(), method_tag_, &call_, &deadline_,
              &request_metadata_,
              has_request_payload_ ? &request_payload_ : nullptr, cq_->cq(),
              cq_->cq(), static_cast<void*>(&tag_)) != GRPC_CALL_OK) {
        return false;
      }
    } else {
      if (!call_details_) {
        call_details_ = new grpc_call_details;
        grpc_call_details_init(call_details_);
      }
      if (grpc_server_request_call(server_->c_server(), &call_, call_details_,
                                   &request_metadata_, cq_->cq(), cq_->cq(),
                                   static_cast<void*>(&tag_)) != GRPC_CALL_OK) {
        return false;
      }
    }
    return true;
  }

  // Needs specialization to account for different processing of metadata
  // in generic API
  bool FinalizeResult(void** tag, bool* status) override;

 private:
  // method_name needs to be specialized between named method and generic
  const char* method_name() const;

  class CallbackCallTag : public grpc_experimental_completion_queue_functor {
   public:
    CallbackCallTag(Server::CallbackRequest<ServerContextType>* req)
        : req_(req) {
      functor_run = &CallbackCallTag::StaticRun;
      // Set inlineable to true since this callback is internally-controlled
      // without taking any locks, and thus does not need to be run from the
      // executor (which triggers a thread hop). This should only be used by
      // internal callbacks like this and not by user application code. The work
      // here is actually non-trivial, but there is no chance of having user
      // locks conflict with each other so it's ok to run inlined.
      inlineable = true;
    }

    // force_run can not be performed on a tag if operations using this tag
    // have been sent to PerformOpsOnCall. It is intended for error conditions
    // that are detected before the operations are internally processed.
    void force_run(bool ok) { Run(ok); }

   private:
    Server::CallbackRequest<ServerContextType>* req_;
    grpc::internal::Call* call_;

    static void StaticRun(grpc_experimental_completion_queue_functor* cb,
                          int ok) {
      static_cast<CallbackCallTag*>(cb)->Run(static_cast<bool>(ok));
    }
    void Run(bool ok) {
      void* ignored = req_;
      bool new_ok = ok;
      GPR_ASSERT(!req_->FinalizeResult(&ignored, &new_ok));
      GPR_ASSERT(ignored == req_);

      int count =
          static_cast<int>(gpr_atm_no_barrier_fetch_add(
              &req_->server_
                   ->callback_unmatched_reqs_count_[req_->method_index_],
              -1)) -
          1;
      if (!ok) {
        // The call has been shutdown.
        // Delete its contents to free up the request.
        delete req_;
        return;
      }

      // If this was the last request in the list or it is below the soft
      // minimum and there are spare requests available, set up a new one.
      if (count == 0 || (count < SOFT_MINIMUM_SPARE_CALLBACK_REQS_PER_METHOD &&
                         req_->server_->callback_reqs_outstanding_ <
                             SOFT_MAXIMUM_CALLBACK_REQS_OUTSTANDING)) {
        auto* new_req = new CallbackRequest<ServerContextType>(
            req_->server_, req_->method_index_, req_->method_,
            req_->method_tag_);
        if (!new_req->Request()) {
          // The server must have just decided to shutdown.
          gpr_atm_no_barrier_fetch_add(
              &new_req->server_
                   ->callback_unmatched_reqs_count_[new_req->method_index_],
              -1);
          delete new_req;
        }
      }

      // Bind the call, deadline, and metadata from what we got
      req_->ctx_.set_call(req_->call_);
      req_->ctx_.cq_ = req_->cq_;
      req_->ctx_.BindDeadlineAndMetadata(req_->deadline_,
                                         &req_->request_metadata_);
      req_->request_metadata_.count = 0;

      // Create a C++ Call to control the underlying core call
      call_ =
          new (grpc_call_arena_alloc(req_->call_, sizeof(grpc::internal::Call)))
              grpc::internal::Call(
                  req_->call_, req_->server_, req_->cq_,
                  req_->server_->max_receive_message_size(),
                  req_->ctx_.set_server_rpc_info(
                      req_->method_name(),
                      (req_->method_ != nullptr)
                          ? req_->method_->method_type()
                          : grpc::internal::RpcMethod::BIDI_STREAMING,
                      req_->server_->interceptor_creators_));

      req_->interceptor_methods_.SetCall(call_);
      req_->interceptor_methods_.SetReverse();
      // Set interception point for RECV INITIAL METADATA
      req_->interceptor_methods_.AddInterceptionHookPoint(
          grpc::experimental::InterceptionHookPoints::
              POST_RECV_INITIAL_METADATA);
      req_->interceptor_methods_.SetRecvInitialMetadata(
          &req_->ctx_.client_metadata_);

      if (req_->has_request_payload_) {
        // Set interception point for RECV MESSAGE
        req_->request_ = req_->method_->handler()->Deserialize(
            req_->call_, req_->request_payload_, &req_->request_status_,
            &req_->handler_data_);
        req_->request_payload_ = nullptr;
        req_->interceptor_methods_.AddInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::POST_RECV_MESSAGE);
        req_->interceptor_methods_.SetRecvMessage(req_->request_, nullptr);
      }

      if (req_->interceptor_methods_.RunInterceptors(
              [this] { ContinueRunAfterInterception(); })) {
        ContinueRunAfterInterception();
      } else {
        // There were interceptors to be run, so ContinueRunAfterInterception
        // will be run when interceptors are done.
      }
    }
    void ContinueRunAfterInterception() {
      auto* handler = (req_->method_ != nullptr)
                          ? req_->method_->handler()
                          : req_->server_->generic_handler_.get();
      handler->RunHandler(grpc::internal::MethodHandler::HandlerParameter(
          call_, &req_->ctx_, req_->request_, req_->request_status_,
          req_->handler_data_, [this] {
            // Recycle this request if there aren't too many outstanding.
            // Note that we don't have to worry about a case where there
            // are no requests waiting to match for this method since that
            // is already taken care of when binding a request to a call.
            // TODO(vjpai): Also don't recycle this request if the dynamic
            //              load no longer justifies it. Consider measuring
            //              dynamic load and setting a target accordingly.
            if (req_->server_->callback_reqs_outstanding_ <
                SOFT_MAXIMUM_CALLBACK_REQS_OUTSTANDING) {
              req_->Clear();
              req_->Setup();
            } else {
              // We can free up this request because there are too many
              delete req_;
              return;
            }
            if (!req_->Request()) {
              // The server must have just decided to shutdown.
              delete req_;
            }
          }));
    }
  };

  void Clear() {
    if (call_details_) {
      delete call_details_;
      call_details_ = nullptr;
    }
    grpc_metadata_array_destroy(&request_metadata_);
    if (has_request_payload_ && request_payload_) {
      grpc_byte_buffer_destroy(request_payload_);
    }
    ctx_.Clear();
    interceptor_methods_.ClearState();
  }

  void Setup() {
    gpr_atm_no_barrier_fetch_add(
        &server_->callback_unmatched_reqs_count_[method_index_], 1);
    grpc_metadata_array_init(&request_metadata_);
    ctx_.Setup(gpr_inf_future(GPR_CLOCK_REALTIME));
    request_payload_ = nullptr;
    request_ = nullptr;
    handler_data_ = nullptr;
    request_status_ = grpc::Status();
  }

  Server* const server_;
  const size_t method_index_;
  grpc::internal::RpcServiceMethod* const method_;
  void* const method_tag_;
  const bool has_request_payload_;
  grpc_byte_buffer* request_payload_;
  void* request_;
  void* handler_data_;
  grpc::Status request_status_;
  grpc_call_details* call_details_ = nullptr;
  grpc_call* call_;
  gpr_timespec deadline_;
  grpc_metadata_array request_metadata_;
  grpc::CompletionQueue* cq_;
  CallbackCallTag tag_;
  ServerContextType ctx_;
  grpc::internal::InterceptorBatchMethodsImpl interceptor_methods_;
};

template <>
bool Server::CallbackRequest<grpc::CallbackServerContext>::FinalizeResult(
    void** /*tag*/, bool* /*status*/) {
  return false;
}

template <>
bool Server::CallbackRequest<
    grpc::GenericCallbackServerContext>::FinalizeResult(void** /*tag*/,
                                                        bool* status) {
  if (*status) {
    deadline_ = call_details_->deadline;
    // TODO(yangg) remove the copy here
    ctx_.method_ = grpc::StringFromCopiedSlice(call_details_->method);
    ctx_.host_ = grpc::StringFromCopiedSlice(call_details_->host);
  }
  grpc_slice_unref(call_details_->method);
  grpc_slice_unref(call_details_->host);
  return false;
}

template <>
const char* Server::CallbackRequest<grpc::CallbackServerContext>::method_name()
    const {
  return method_->name();
}

template <>
const char* Server::CallbackRequest<
    grpc::GenericCallbackServerContext>::method_name() const {
  return ctx_.method().c_str();
}

// Implementation of ThreadManager. Each instance of SyncRequestThreadManager
// manages a pool of threads that poll for incoming Sync RPCs and call the
// appropriate RPC handlers
class Server::SyncRequestThreadManager : public grpc::ThreadManager {
 public:
  SyncRequestThreadManager(Server* server, grpc::CompletionQueue* server_cq,
                           std::shared_ptr<GlobalCallbacks> global_callbacks,
                           grpc_resource_quota* rq, int min_pollers,
                           int max_pollers, int cq_timeout_msec)
      : ThreadManager("SyncServer", rq, min_pollers, max_pollers),
        server_(server),
        server_cq_(server_cq),
        cq_timeout_msec_(cq_timeout_msec),
        global_callbacks_(std::move(global_callbacks)) {}

  WorkStatus PollForWork(void** tag, bool* ok) override {
    *tag = nullptr;
    // TODO(ctiller): workaround for GPR_TIMESPAN based deadlines not working
    // right now
    gpr_timespec deadline =
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                     gpr_time_from_millis(cq_timeout_msec_, GPR_TIMESPAN));

    switch (server_cq_->AsyncNext(tag, ok, deadline)) {
      case grpc::CompletionQueue::TIMEOUT:
        return TIMEOUT;
      case grpc::CompletionQueue::SHUTDOWN:
        return SHUTDOWN;
      case grpc::CompletionQueue::GOT_EVENT:
        return WORK_FOUND;
    }

    GPR_UNREACHABLE_CODE(return TIMEOUT);
  }

  void DoWork(void* tag, bool ok, bool resources) override {
    SyncRequest* sync_req = static_cast<SyncRequest*>(tag);

    if (!sync_req) {
      // No tag. Nothing to work on. This is an unlikley scenario and possibly a
      // bug in RPC Manager implementation.
      gpr_log(GPR_ERROR, "Sync server. DoWork() was called with NULL tag");
      return;
    }

    if (ok) {
      // Calldata takes ownership of the completion queue and interceptors
      // inside sync_req
      auto* cd = new SyncRequest::CallData(server_, sync_req);
      // Prepare for the next request
      if (!IsShutdown()) {
        sync_req->SetupRequest();  // Create new completion queue for sync_req
        sync_req->Request(server_->c_server(), server_cq_->cq());
      }

      GPR_TIMER_SCOPE("cd.Run()", 0);
      cd->Run(global_callbacks_, resources);
    }
    // TODO (sreek) If ok is false here (which it isn't in case of
    // grpc_request_registered_call), we should still re-queue the request
    // object
  }

  void AddSyncMethod(grpc::internal::RpcServiceMethod* method, void* tag) {
    sync_requests_.emplace_back(new SyncRequest(method, tag));
  }

  void AddUnknownSyncMethod() {
    if (!sync_requests_.empty()) {
      unknown_method_.reset(new grpc::internal::RpcServiceMethod(
          "unknown", grpc::internal::RpcMethod::BIDI_STREAMING,
          new grpc::internal::UnknownMethodHandler));
      sync_requests_.emplace_back(
          new SyncRequest(unknown_method_.get(), nullptr));
    }
  }

  void Shutdown() override {
    ThreadManager::Shutdown();
    server_cq_->Shutdown();
  }

  void Wait() override {
    ThreadManager::Wait();
    // Drain any pending items from the queue
    void* tag;
    bool ok;
    while (server_cq_->Next(&tag, &ok)) {
      if (ok) {
        // If a request was pulled off the queue, it means that the thread
        // handling the request added it to the completion queue after shutdown
        // was called - because the thread had already started and checked the
        // shutdown flag before shutdown was called. In this case, we simply
        // clean it up here, *after* calling wait on all the worker threads, at
        // which point we are certain no in-flight requests will add more to the
        // queue. This fixes an intermittent memory leak on shutdown.
        SyncRequest* sync_req = static_cast<SyncRequest*>(tag);
        sync_req->PostShutdownCleanup();
      }
    }
  }

  void Start() {
    if (!sync_requests_.empty()) {
      for (const auto& value : sync_requests_) {
        value->SetupRequest();
        value->Request(server_->c_server(), server_cq_->cq());
      }

      Initialize();  // ThreadManager's Initialize()
    }
  }

 private:
  Server* server_;
  grpc::CompletionQueue* server_cq_;
  int cq_timeout_msec_;
  std::vector<std::unique_ptr<SyncRequest>> sync_requests_;
  std::unique_ptr<grpc::internal::RpcServiceMethod> unknown_method_;
  std::shared_ptr<Server::GlobalCallbacks> global_callbacks_;
};

static grpc::internal::GrpcLibraryInitializer g_gli_initializer;
Server::Server(
    grpc::ChannelArguments* args,
    std::shared_ptr<std::vector<std::unique_ptr<grpc::ServerCompletionQueue>>>
        sync_server_cqs,
    int min_pollers, int max_pollers, int sync_cq_timeout_msec,
    std::vector<std::shared_ptr<grpc::internal::ExternalConnectionAcceptorImpl>>
        acceptors,
    grpc_resource_quota* server_rq,
    std::vector<
        std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>>
        interceptor_creators)
    : acceptors_(std::move(acceptors)),
      interceptor_creators_(std::move(interceptor_creators)),
      max_receive_message_size_(INT_MIN),
      sync_server_cqs_(std::move(sync_server_cqs)),
      started_(false),
      shutdown_(false),
      shutdown_notified_(false),
      server_(nullptr),
      server_initializer_(new grpc_impl::ServerInitializer(this)),
      health_check_service_disabled_(false) {
  g_gli_initializer.summon();
  gpr_once_init(&grpc::g_once_init_callbacks, grpc::InitGlobalCallbacks);
  global_callbacks_ = grpc::g_callbacks;
  global_callbacks_->UpdateArguments(args);

  if (sync_server_cqs_ != nullptr) {
    bool default_rq_created = false;
    if (server_rq == nullptr) {
      server_rq = grpc_resource_quota_create("SyncServer-default-rq");
      grpc_resource_quota_set_max_threads(server_rq,
                                          DEFAULT_MAX_SYNC_SERVER_THREADS);
      default_rq_created = true;
    }

    for (const auto& it : *sync_server_cqs_) {
      sync_req_mgrs_.emplace_back(new SyncRequestThreadManager(
          this, it.get(), global_callbacks_, server_rq, min_pollers,
          max_pollers, sync_cq_timeout_msec));
    }

    if (default_rq_created) {
      grpc_resource_quota_unref(server_rq);
    }
  }

  for (auto& acceptor : acceptors_) {
    acceptor->SetToChannelArgs(args);
  }

  grpc_channel_args channel_args;
  args->SetChannelArgs(&channel_args);

  for (size_t i = 0; i < channel_args.num_args; i++) {
    if (0 == strcmp(channel_args.args[i].key,
                    grpc::kHealthCheckServiceInterfaceArg)) {
      if (channel_args.args[i].value.pointer.p == nullptr) {
        health_check_service_disabled_ = true;
      } else {
        health_check_service_.reset(
            static_cast<grpc::HealthCheckServiceInterface*>(
                channel_args.args[i].value.pointer.p));
      }
    }
    if (0 ==
        strcmp(channel_args.args[i].key, GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH)) {
      max_receive_message_size_ = channel_args.args[i].value.integer;
    }
  }
  server_ = grpc_server_create(&channel_args, nullptr);
}

Server::~Server() {
  {
    grpc::internal::ReleasableMutexLock lock(&mu_);
    if (started_ && !shutdown_) {
      lock.Unlock();
      Shutdown();
    } else if (!started_) {
      // Shutdown the completion queues
      for (const auto& value : sync_req_mgrs_) {
        value->Shutdown();
      }
      if (callback_cq_ != nullptr) {
        callback_cq_->Shutdown();
        callback_cq_ = nullptr;
      }
    }
  }

  grpc_server_destroy(server_);
  for (auto& per_method_count : callback_unmatched_reqs_count_) {
    // There should be no more unmatched callbacks for any method
    // as each request is failed by Shutdown. Check that this actually
    // happened
    GPR_ASSERT(static_cast<int>(gpr_atm_no_barrier_load(&per_method_count)) ==
               0);
  }
}

void Server::SetGlobalCallbacks(GlobalCallbacks* callbacks) {
  GPR_ASSERT(!grpc::g_callbacks);
  GPR_ASSERT(callbacks);
  grpc::g_callbacks.reset(callbacks);
}

grpc_server* Server::c_server() { return server_; }

std::shared_ptr<grpc::Channel> Server::InProcessChannel(
    const grpc::ChannelArguments& args) {
  grpc_channel_args channel_args = args.c_channel_args();
  return grpc::CreateChannelInternal(
      "inproc", grpc_inproc_channel_create(server_, &channel_args, nullptr),
      std::vector<std::unique_ptr<
          grpc::experimental::ClientInterceptorFactoryInterface>>());
}

std::shared_ptr<grpc::Channel>
Server::experimental_type::InProcessChannelWithInterceptors(
    const grpc::ChannelArguments& args,
    std::vector<
        std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators) {
  grpc_channel_args channel_args = args.c_channel_args();
  return grpc::CreateChannelInternal(
      "inproc",
      grpc_inproc_channel_create(server_->server_, &channel_args, nullptr),
      std::move(interceptor_creators));
}

static grpc_server_register_method_payload_handling PayloadHandlingForMethod(
    grpc::internal::RpcServiceMethod* method) {
  switch (method->method_type()) {
    case grpc::internal::RpcMethod::NORMAL_RPC:
    case grpc::internal::RpcMethod::SERVER_STREAMING:
      return GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER;
    case grpc::internal::RpcMethod::CLIENT_STREAMING:
    case grpc::internal::RpcMethod::BIDI_STREAMING:
      return GRPC_SRM_PAYLOAD_NONE;
  }
  GPR_UNREACHABLE_CODE(return GRPC_SRM_PAYLOAD_NONE;);
}

bool Server::RegisterService(const grpc::string* host, grpc::Service* service) {
  bool has_async_methods = service->has_async_methods();
  if (has_async_methods) {
    GPR_ASSERT(service->server_ == nullptr &&
               "Can only register an asynchronous service against one server.");
    service->server_ = this;
  }

  const char* method_name = nullptr;

  for (const auto& method : service->methods_) {
    if (method.get() == nullptr) {  // Handled by generic service if any.
      continue;
    }

    void* method_registration_tag = grpc_server_register_method(
        server_, method->name(), host ? host->c_str() : nullptr,
        PayloadHandlingForMethod(method.get()), 0);
    if (method_registration_tag == nullptr) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times",
              method->name());
      return false;
    }

    if (method->handler() == nullptr) {  // Async method without handler
      method->set_server_tag(method_registration_tag);
    } else if (method->api_type() ==
               grpc::internal::RpcServiceMethod::ApiType::SYNC) {
      for (const auto& value : sync_req_mgrs_) {
        value->AddSyncMethod(method.get(), method_registration_tag);
      }
    } else {
      // a callback method. Register at least some callback requests
      callback_unmatched_reqs_count_.push_back(0);
      auto method_index = callback_unmatched_reqs_count_.size() - 1;
      // TODO(vjpai): Register these dynamically based on need
      for (int i = 0; i < DEFAULT_CALLBACK_REQS_PER_METHOD; i++) {
        callback_reqs_to_start_.push_back(
            new CallbackRequest<grpc::CallbackServerContext>(
                this, method_index, method.get(), method_registration_tag));
      }
      // Enqueue it so that it will be Request'ed later after all request
      // matchers are created at core server startup
    }

    method_name = method->name();
  }

  // Parse service name.
  if (method_name != nullptr) {
    std::stringstream ss(method_name);
    grpc::string service_name;
    if (std::getline(ss, service_name, '/') &&
        std::getline(ss, service_name, '/')) {
      services_.push_back(service_name);
    }
  }
  return true;
}

void Server::RegisterAsyncGenericService(grpc::AsyncGenericService* service) {
  GPR_ASSERT(service->server_ == nullptr &&
             "Can only register an async generic service against one server.");
  service->server_ = this;
  has_async_generic_service_ = true;
}

void Server::RegisterCallbackGenericService(
    grpc::CallbackGenericService* service) {
  GPR_ASSERT(
      service->server_ == nullptr &&
      "Can only register a callback generic service against one server.");
  service->server_ = this;
  has_callback_generic_service_ = true;
  generic_handler_.reset(service->Handler());

  callback_unmatched_reqs_count_.push_back(0);
  auto method_index = callback_unmatched_reqs_count_.size() - 1;
  // TODO(vjpai): Register these dynamically based on need
  for (int i = 0; i < DEFAULT_CALLBACK_REQS_PER_METHOD; i++) {
    callback_reqs_to_start_.push_back(
        new CallbackRequest<grpc::GenericCallbackServerContext>(
            this, method_index, nullptr, nullptr));
  }
}

int Server::AddListeningPort(const grpc::string& addr,
                             grpc::ServerCredentials* creds) {
  GPR_ASSERT(!started_);
  int port = creds->AddPortToServer(addr, server_);
  global_callbacks_->AddPort(this, addr, creds, port);
  return port;
}

void Server::Start(grpc::ServerCompletionQueue** cqs, size_t num_cqs) {
  GPR_ASSERT(!started_);
  global_callbacks_->PreServerStart(this);
  started_ = true;

  // Only create default health check service when user did not provide an
  // explicit one.
  grpc::ServerCompletionQueue* health_check_cq = nullptr;
  grpc::DefaultHealthCheckService::HealthCheckServiceImpl*
      default_health_check_service_impl = nullptr;
  if (health_check_service_ == nullptr && !health_check_service_disabled_ &&
      grpc::DefaultHealthCheckServiceEnabled()) {
    auto* default_hc_service = new grpc::DefaultHealthCheckService;
    health_check_service_.reset(default_hc_service);
    // We create a non-polling CQ to avoid impacting application
    // performance.  This ensures that we don't introduce thread hops
    // for application requests that wind up on this CQ, which is polled
    // in its own thread.
    health_check_cq = new grpc::ServerCompletionQueue(
        GRPC_CQ_NEXT, GRPC_CQ_NON_POLLING, nullptr);
    grpc_server_register_completion_queue(server_, health_check_cq->cq(),
                                          nullptr);
    default_health_check_service_impl =
        default_hc_service->GetHealthCheckService(
            std::unique_ptr<grpc::ServerCompletionQueue>(health_check_cq));
    RegisterService(nullptr, default_health_check_service_impl);
  }

  for (auto& acceptor : acceptors_) {
    acceptor->GetCredentials()->AddPortToServer(acceptor->name(), server_);
  }

  // If this server uses callback methods, then create a callback generic
  // service to handle any unimplemented methods using the default reactor
  // creator
  if (!callback_reqs_to_start_.empty() && !has_callback_generic_service_) {
    unimplemented_service_.reset(new grpc::CallbackGenericService);
    RegisterCallbackGenericService(unimplemented_service_.get());
  }

  grpc_server_start(server_);

  if (!has_async_generic_service_ && !has_callback_generic_service_) {
    for (const auto& value : sync_req_mgrs_) {
      value->AddUnknownSyncMethod();
    }

    for (size_t i = 0; i < num_cqs; i++) {
#ifndef NDEBUG
      cq_list_.push_back(cqs[i]);
#endif
      if (cqs[i]->IsFrequentlyPolled()) {
        new UnimplementedAsyncRequest(this, cqs[i]);
      }
    }
    if (health_check_cq != nullptr) {
      new UnimplementedAsyncRequest(this, health_check_cq);
    }
  }

  // If this server has any support for synchronous methods (has any sync
  // server CQs), make sure that we have a ResourceExhausted handler
  // to deal with the case of thread exhaustion
  if (sync_server_cqs_ != nullptr && !sync_server_cqs_->empty()) {
    resource_exhausted_handler_.reset(
        new grpc::internal::ResourceExhaustedHandler);
  }

  for (const auto& value : sync_req_mgrs_) {
    value->Start();
  }

  for (auto* cbreq : callback_reqs_to_start_) {
    GPR_ASSERT(cbreq->Request());
  }
  callback_reqs_to_start_.clear();

  if (default_health_check_service_impl != nullptr) {
    default_health_check_service_impl->StartServingThread();
  }

  for (auto& acceptor : acceptors_) {
    acceptor->Start();
  }
}

void Server::ShutdownInternal(gpr_timespec deadline) {
  grpc::internal::MutexLock lock(&mu_);
  if (shutdown_) {
    return;
  }

  shutdown_ = true;

  for (auto& acceptor : acceptors_) {
    acceptor->Shutdown();
  }

  /// The completion queue to use for server shutdown completion notification
  grpc::CompletionQueue shutdown_cq;
  grpc::ShutdownTag shutdown_tag;  // Dummy shutdown tag
  grpc_server_shutdown_and_notify(server_, shutdown_cq.cq(), &shutdown_tag);

  shutdown_cq.Shutdown();

  void* tag;
  bool ok;
  grpc::CompletionQueue::NextStatus status =
      shutdown_cq.AsyncNext(&tag, &ok, deadline);

  // If this timed out, it means we are done with the grace period for a clean
  // shutdown. We should force a shutdown now by cancelling all inflight calls
  if (status == grpc::CompletionQueue::NextStatus::TIMEOUT) {
    grpc_server_cancel_all_calls(server_);
  }
  // Else in case of SHUTDOWN or GOT_EVENT, it means that the server has
  // successfully shutdown

  // Shutdown all ThreadManagers. This will try to gracefully stop all the
  // threads in the ThreadManagers (once they process any inflight requests)
  for (const auto& value : sync_req_mgrs_) {
    value->Shutdown();  // ThreadManager's Shutdown()
  }

  // Wait for threads in all ThreadManagers to terminate
  for (const auto& value : sync_req_mgrs_) {
    value->Wait();
  }

  // Wait for all outstanding callback requests to complete
  // (whether waiting for a match or already active).
  // We know that no new requests will be created after this point
  // because they are only created at server startup time or when
  // we have a successful match on a request. During the shutdown phase,
  // requests that have not yet matched will be failed rather than
  // allowed to succeed, which will cause the server to delete the
  // request and decrement the count. Possibly a request will match before
  // the shutdown but then find that shutdown has already started by the
  // time it tries to register a new request. In that case, the registration
  // will report a failure, indicating a shutdown and again we won't end
  // up incrementing the counter.
  {
    grpc::internal::MutexLock cblock(&callback_reqs_mu_);
    callback_reqs_done_cv_.WaitUntil(
        &callback_reqs_mu_, [this] { return callback_reqs_outstanding_ == 0; });
  }

  // Shutdown the callback CQ. The CQ is owned by its own shutdown tag, so it
  // will delete itself at true shutdown.
  if (callback_cq_ != nullptr) {
    callback_cq_->Shutdown();
    callback_cq_ = nullptr;
  }

  // Drain the shutdown queue (if the previous call to AsyncNext() timed out
  // and we didn't remove the tag from the queue yet)
  while (shutdown_cq.Next(&tag, &ok)) {
    // Nothing to be done here. Just ignore ok and tag values
  }

  shutdown_notified_ = true;
  shutdown_cv_.Broadcast();

#ifndef NDEBUG
  // Unregister this server with the CQs passed into it by the user so that
  // those can be checked for properly-ordered shutdown.
  for (auto* cq : cq_list_) {
    cq->UnregisterServer(this);
  }
  cq_list_.clear();
#endif
}

void Server::Wait() {
  grpc::internal::MutexLock lock(&mu_);
  while (started_ && !shutdown_notified_) {
    shutdown_cv_.Wait(&mu_);
  }
}

void Server::PerformOpsOnCall(grpc::internal::CallOpSetInterface* ops,
                              grpc::internal::Call* call) {
  ops->FillOps(call);
}

bool Server::UnimplementedAsyncRequest::FinalizeResult(void** tag,
                                                       bool* status) {
  if (GenericAsyncRequest::FinalizeResult(tag, status)) {
    // We either had no interceptors run or we are done intercepting
    if (*status) {
      new UnimplementedAsyncRequest(server_, cq_);
      new UnimplementedAsyncResponse(this);
    } else {
      delete this;
    }
  } else {
    // The tag was swallowed due to interception. We will see it again.
  }
  return false;
}

Server::UnimplementedAsyncResponse::UnimplementedAsyncResponse(
    UnimplementedAsyncRequest* request)
    : request_(request) {
  grpc::Status status(grpc::StatusCode::UNIMPLEMENTED, "");
  grpc::internal::UnknownMethodHandler::FillOps(request_->context(), this);
  request_->stream()->call_.PerformOps(this);
}

grpc::ServerInitializer* Server::initializer() {
  return server_initializer_.get();
}

grpc::CompletionQueue* Server::CallbackCQ() {
  // TODO(vjpai): Consider using a single global CQ for the default CQ
  // if there is no explicit per-server CQ registered
  grpc::internal::MutexLock l(&mu_);
  if (callback_cq_ == nullptr) {
    auto* shutdown_callback = new grpc::ShutdownCallback;
    callback_cq_ = new grpc::CompletionQueue(grpc_completion_queue_attributes{
        GRPC_CQ_CURRENT_VERSION, GRPC_CQ_CALLBACK, GRPC_CQ_DEFAULT_POLLING,
        shutdown_callback});

    // Transfer ownership of the new cq to its own shutdown callback
    shutdown_callback->TakeCQ(callback_cq_);
  }
  return callback_cq_;
}

}  // namespace grpc_impl
