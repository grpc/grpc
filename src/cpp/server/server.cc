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

#include <grpc++/server.h>

#include <sstream>
#include <utility>

#include <grpc++/completion_queue.h>
#include <grpc++/generic/async_generic_service.h>
#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/impl/method_handler_impl.h>
#include <grpc++/impl/rpc_service_method.h>
#include <grpc++/impl/server_initializer.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server_context.h>
#include <grpc++/support/time.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"
#include "src/cpp/server/thread_pool_interface.h"

namespace grpc {

class DefaultGlobalCallbacks GRPC_FINAL : public Server::GlobalCallbacks {
 public:
  ~DefaultGlobalCallbacks() GRPC_OVERRIDE {}
  void PreSynchronousRequest(ServerContext* context) GRPC_OVERRIDE {}
  void PostSynchronousRequest(ServerContext* context) GRPC_OVERRIDE {}
};

static std::shared_ptr<Server::GlobalCallbacks> g_callbacks = nullptr;
static gpr_once g_once_init_callbacks = GPR_ONCE_INIT;

static void InitGlobalCallbacks() {
  if (g_callbacks == nullptr) {
    g_callbacks.reset(new DefaultGlobalCallbacks());
  }
}

class Server::UnimplementedAsyncRequestContext {
 protected:
  UnimplementedAsyncRequestContext() : generic_stream_(&server_context_) {}

  GenericServerContext server_context_;
  GenericServerAsyncReaderWriter generic_stream_;
};

class Server::UnimplementedAsyncRequest GRPC_FINAL
    : public UnimplementedAsyncRequestContext,
      public GenericAsyncRequest {
 public:
  UnimplementedAsyncRequest(Server* server, ServerCompletionQueue* cq)
      : GenericAsyncRequest(server, &server_context_, &generic_stream_, cq, cq,
                            NULL, false),
        server_(server),
        cq_(cq) {}

  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE;

  ServerContext* context() { return &server_context_; }
  GenericServerAsyncReaderWriter* stream() { return &generic_stream_; }

 private:
  Server* const server_;
  ServerCompletionQueue* const cq_;
};

typedef SneakyCallOpSet<CallOpSendInitialMetadata, CallOpServerSendStatus>
    UnimplementedAsyncResponseOp;
class Server::UnimplementedAsyncResponse GRPC_FINAL
    : public UnimplementedAsyncResponseOp {
 public:
  UnimplementedAsyncResponse(UnimplementedAsyncRequest* request);
  ~UnimplementedAsyncResponse() { delete request_; }

  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
    bool r = UnimplementedAsyncResponseOp::FinalizeResult(tag, status);
    delete this;
    return r;
  }

 private:
  UnimplementedAsyncRequest* const request_;
};

class Server::ShutdownRequest GRPC_FINAL : public CompletionQueueTag {
 public:
  bool FinalizeResult(void** tag, bool* status) {
    delete this;
    return false;
  }
};

class Server::SyncRequest GRPC_FINAL : public CompletionQueueTag {
 public:
  SyncRequest(RpcServiceMethod* method, void* tag)
      : method_(method),
        tag_(tag),
        in_flight_(false),
        has_request_payload_(method->method_type() == RpcMethod::NORMAL_RPC ||
                             method->method_type() ==
                                 RpcMethod::SERVER_STREAMING),
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

  static SyncRequest* Wait(CompletionQueue* cq, bool* ok) {
    void* tag = nullptr;
    *ok = false;
    if (!cq->Next(&tag, ok)) {
      return nullptr;
    }
    auto* mrd = static_cast<SyncRequest*>(tag);
    GPR_ASSERT(mrd->in_flight_);
    return mrd;
  }

  static bool AsyncWait(CompletionQueue* cq, SyncRequest** req, bool* ok,
                        gpr_timespec deadline) {
    void* tag = nullptr;
    *ok = false;
    switch (cq->AsyncNext(&tag, ok, deadline)) {
      case CompletionQueue::TIMEOUT:
        *req = nullptr;
        return true;
      case CompletionQueue::SHUTDOWN:
        *req = nullptr;
        return false;
      case CompletionQueue::GOT_EVENT:
        *req = static_cast<SyncRequest*>(tag);
        GPR_ASSERT((*req)->in_flight_);
        return true;
    }
    GPR_UNREACHABLE_CODE(return false);
  }

  void SetupRequest() { cq_ = grpc_completion_queue_create(nullptr); }

  void TeardownRequest() {
    grpc_completion_queue_destroy(cq_);
    cq_ = nullptr;
  }

  void Request(grpc_server* server, grpc_completion_queue* notify_cq) {
    GPR_ASSERT(cq_ && !in_flight_);
    in_flight_ = true;
    if (tag_) {
      GPR_ASSERT(GRPC_CALL_OK ==
                 grpc_server_request_registered_call(
                     server, tag_, &call_, &deadline_, &request_metadata_,
                     has_request_payload_ ? &request_payload_ : nullptr, cq_,
                     notify_cq, this));
    } else {
      if (!call_details_) {
        call_details_ = new grpc_call_details;
        grpc_call_details_init(call_details_);
      }
      GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                     server, &call_, call_details_,
                                     &request_metadata_, cq_, notify_cq, this));
    }
  }

  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
    if (!*status) {
      grpc_completion_queue_destroy(cq_);
    }
    if (call_details_) {
      deadline_ = call_details_->deadline;
      grpc_call_details_destroy(call_details_);
      grpc_call_details_init(call_details_);
    }
    return true;
  }

  class CallData GRPC_FINAL {
   public:
    explicit CallData(Server* server, SyncRequest* mrd)
        : cq_(mrd->cq_),
          call_(mrd->call_, server, &cq_, server->max_message_size_),
          ctx_(mrd->deadline_, mrd->request_metadata_.metadata,
               mrd->request_metadata_.count),
          has_request_payload_(mrd->has_request_payload_),
          request_payload_(mrd->request_payload_),
          method_(mrd->method_) {
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

    void Run(std::shared_ptr<GlobalCallbacks> global_callbacks) {
      ctx_.BeginCompletionOp(&call_);
      global_callbacks->PreSynchronousRequest(&ctx_);
      method_->handler()->RunHandler(MethodHandler::HandlerParameter(
          &call_, &ctx_, request_payload_, call_.max_message_size()));
      global_callbacks->PostSynchronousRequest(&ctx_);
      request_payload_ = nullptr;
      void* ignored_tag;
      bool ignored_ok;
      cq_.Shutdown();
      GPR_ASSERT(cq_.Next(&ignored_tag, &ignored_ok) == false);
    }

   private:
    CompletionQueue cq_;
    Call call_;
    ServerContext ctx_;
    const bool has_request_payload_;
    grpc_byte_buffer* request_payload_;
    RpcServiceMethod* const method_;
  };

 private:
  RpcServiceMethod* const method_;
  void* const tag_;
  bool in_flight_;
  const bool has_request_payload_;
  uint32_t incoming_flags_;
  grpc_call* call_;
  grpc_call_details* call_details_;
  gpr_timespec deadline_;
  grpc_metadata_array request_metadata_;
  grpc_byte_buffer* request_payload_;
  grpc_completion_queue* cq_;
};

static internal::GrpcLibraryInitializer g_gli_initializer;
Server::Server(ThreadPoolInterface* thread_pool, bool thread_pool_owned,
               int max_message_size, ChannelArguments* args)
    : max_message_size_(max_message_size),
      started_(false),
      shutdown_(false),
      num_running_cb_(0),
      sync_methods_(new std::list<SyncRequest>),
      has_generic_service_(false),
      server_(nullptr),
      thread_pool_(thread_pool),
      thread_pool_owned_(thread_pool_owned),
      server_initializer_(new ServerInitializer(this)) {
  g_gli_initializer.summon();
  gpr_once_init(&g_once_init_callbacks, InitGlobalCallbacks);
  global_callbacks_ = g_callbacks;
  global_callbacks_->UpdateArguments(args);
  grpc_channel_args channel_args;
  args->SetChannelArgs(&channel_args);
  server_ = grpc_server_create(&channel_args, nullptr);
  if (thread_pool_ == nullptr) {
    grpc_server_register_non_listening_completion_queue(server_, cq_.cq(),
                                                        nullptr);
  } else {
    grpc_server_register_completion_queue(server_, cq_.cq(), nullptr);
  }
}

Server::~Server() {
  {
    grpc::unique_lock<grpc::mutex> lock(mu_);
    if (started_ && !shutdown_) {
      lock.unlock();
      Shutdown();
    } else if (!started_) {
      cq_.Shutdown();
    }
  }
  void* got_tag;
  bool ok;
  GPR_ASSERT(!cq_.Next(&got_tag, &ok));
  grpc_server_destroy(server_);
  if (thread_pool_owned_) {
    delete thread_pool_;
  }
  delete sync_methods_;
}

void Server::SetGlobalCallbacks(GlobalCallbacks* callbacks) {
  GPR_ASSERT(g_callbacks == nullptr);
  GPR_ASSERT(callbacks != nullptr);
  g_callbacks.reset(callbacks);
}

grpc_server* Server::c_server() { return server_; }

CompletionQueue* Server::completion_queue() { return &cq_; }

static grpc_server_register_method_payload_handling PayloadHandlingForMethod(
    RpcServiceMethod* method) {
  switch (method->method_type()) {
    case RpcMethod::NORMAL_RPC:
    case RpcMethod::SERVER_STREAMING:
      return GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER;
    case RpcMethod::CLIENT_STREAMING:
    case RpcMethod::BIDI_STREAMING:
      return GRPC_SRM_PAYLOAD_NONE;
  }
  GPR_UNREACHABLE_CODE(return GRPC_SRM_PAYLOAD_NONE;);
}

bool Server::RegisterService(const grpc::string* host, Service* service) {
  bool has_async_methods = service->has_async_methods();
  if (has_async_methods) {
    GPR_ASSERT(service->server_ == nullptr &&
               "Can only register an asynchronous service against one server.");
    service->server_ = this;
  }
  const char* method_name = nullptr;
  for (auto it = service->methods_.begin(); it != service->methods_.end();
       ++it) {
    if (it->get() == nullptr) {  // Handled by generic service if any.
      continue;
    }
    RpcServiceMethod* method = it->get();
    void* tag = grpc_server_register_method(
        server_, method->name(), host ? host->c_str() : nullptr,
        PayloadHandlingForMethod(method), 0);
    if (tag == nullptr) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times",
              method->name());
      return false;
    }
    if (method->handler() == nullptr) {
      method->set_server_tag(tag);
    } else {
      sync_methods_->emplace_back(method, tag);
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

void Server::RegisterAsyncGenericService(AsyncGenericService* service) {
  GPR_ASSERT(service->server_ == nullptr &&
             "Can only register an async generic service against one server.");
  service->server_ = this;
  has_generic_service_ = true;
}

int Server::AddListeningPort(const grpc::string& addr,
                             ServerCredentials* creds) {
  GPR_ASSERT(!started_);
  return creds->AddPortToServer(addr, server_);
}

bool Server::Start(ServerCompletionQueue** cqs, size_t num_cqs) {
  GPR_ASSERT(!started_);
  started_ = true;
  grpc_server_start(server_);

  if (!has_generic_service_) {
    if (!sync_methods_->empty()) {
      unknown_method_.reset(new RpcServiceMethod(
          "unknown", RpcMethod::BIDI_STREAMING, new UnknownMethodHandler));
      // Use of emplace_back with just constructor arguments is not accepted
      // here by gcc-4.4 because it can't match the anonymous nullptr with a
      // proper constructor implicitly. Construct the object and use push_back.
      sync_methods_->push_back(SyncRequest(unknown_method_.get(), nullptr));
    }
    for (size_t i = 0; i < num_cqs; i++) {
      if (cqs[i]->IsFrequentlyPolled()) {
        new UnimplementedAsyncRequest(this, cqs[i]);
      }
    }
  }
  // Start processing rpcs.
  if (!sync_methods_->empty()) {
    for (auto m = sync_methods_->begin(); m != sync_methods_->end(); m++) {
      m->SetupRequest();
      m->Request(server_, cq_.cq());
    }

    ScheduleCallback();
  }

  return true;
}

void Server::ShutdownInternal(gpr_timespec deadline) {
  grpc::unique_lock<grpc::mutex> lock(mu_);
  if (started_ && !shutdown_) {
    shutdown_ = true;
    grpc_server_shutdown_and_notify(server_, cq_.cq(), new ShutdownRequest());
    cq_.Shutdown();
    lock.unlock();
    // Spin, eating requests until the completion queue is completely shutdown.
    // If the deadline expires then cancel anything that's pending and keep
    // spinning forever until the work is actually drained.
    // Since nothing else needs to touch state guarded by mu_, holding it
    // through this loop is fine.
    SyncRequest* request;
    bool ok;
    while (SyncRequest::AsyncWait(&cq_, &request, &ok, deadline)) {
      if (request == NULL) {  // deadline expired
        grpc_server_cancel_all_calls(server_);
        deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
      } else if (ok) {
        SyncRequest::CallData call_data(this, request);
      }
    }
    lock.lock();

    // Wait for running callbacks to finish.
    while (num_running_cb_ != 0) {
      callback_cv_.wait(lock);
    }
  }
}

void Server::Wait() {
  grpc::unique_lock<grpc::mutex> lock(mu_);
  while (num_running_cb_ != 0) {
    callback_cv_.wait(lock);
  }
}

void Server::PerformOpsOnCall(CallOpSetInterface* ops, Call* call) {
  static const size_t MAX_OPS = 8;
  size_t nops = 0;
  grpc_op cops[MAX_OPS];
  ops->FillOps(cops, &nops);
  auto result = grpc_call_start_batch(call->call(), cops, nops, ops, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == result);
}

ServerInterface::BaseAsyncRequest::BaseAsyncRequest(
    ServerInterface* server, ServerContext* context,
    ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq, void* tag,
    bool delete_on_finalize)
    : server_(server),
      context_(context),
      stream_(stream),
      call_cq_(call_cq),
      tag_(tag),
      delete_on_finalize_(delete_on_finalize),
      call_(nullptr) {
  memset(&initial_metadata_array_, 0, sizeof(initial_metadata_array_));
}

bool ServerInterface::BaseAsyncRequest::FinalizeResult(void** tag,
                                                       bool* status) {
  if (*status) {
    for (size_t i = 0; i < initial_metadata_array_.count; i++) {
      context_->client_metadata_.insert(
          std::pair<grpc::string_ref, grpc::string_ref>(
              initial_metadata_array_.metadata[i].key,
              grpc::string_ref(
                  initial_metadata_array_.metadata[i].value,
                  initial_metadata_array_.metadata[i].value_length)));
    }
  }
  grpc_metadata_array_destroy(&initial_metadata_array_);
  context_->set_call(call_);
  context_->cq_ = call_cq_;
  Call call(call_, server_, call_cq_, server_->max_message_size());
  if (*status && call_) {
    context_->BeginCompletionOp(&call);
  }
  // just the pointers inside call are copied here
  stream_->BindCall(&call);
  *tag = tag_;
  if (delete_on_finalize_) {
    delete this;
  }
  return true;
}

ServerInterface::RegisteredAsyncRequest::RegisteredAsyncRequest(
    ServerInterface* server, ServerContext* context,
    ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq, void* tag)
    : BaseAsyncRequest(server, context, stream, call_cq, tag, true) {}

void ServerInterface::RegisteredAsyncRequest::IssueRequest(
    void* registered_method, grpc_byte_buffer** payload,
    ServerCompletionQueue* notification_cq) {
  grpc_server_request_registered_call(
      server_->server(), registered_method, &call_, &context_->deadline_,
      &initial_metadata_array_, payload, call_cq_->cq(), notification_cq->cq(),
      this);
}

ServerInterface::GenericAsyncRequest::GenericAsyncRequest(
    ServerInterface* server, GenericServerContext* context,
    ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    ServerCompletionQueue* notification_cq, void* tag, bool delete_on_finalize)
    : BaseAsyncRequest(server, context, stream, call_cq, tag,
                       delete_on_finalize) {
  grpc_call_details_init(&call_details_);
  GPR_ASSERT(notification_cq);
  GPR_ASSERT(call_cq);
  grpc_server_request_call(server->server(), &call_, &call_details_,
                           &initial_metadata_array_, call_cq->cq(),
                           notification_cq->cq(), this);
}

bool ServerInterface::GenericAsyncRequest::FinalizeResult(void** tag,
                                                          bool* status) {
  // TODO(yangg) remove the copy here.
  if (*status) {
    static_cast<GenericServerContext*>(context_)->method_ =
        call_details_.method;
    static_cast<GenericServerContext*>(context_)->host_ = call_details_.host;
  }
  gpr_free(call_details_.method);
  gpr_free(call_details_.host);
  return BaseAsyncRequest::FinalizeResult(tag, status);
}

bool Server::UnimplementedAsyncRequest::FinalizeResult(void** tag,
                                                       bool* status) {
  if (GenericAsyncRequest::FinalizeResult(tag, status) && *status) {
    new UnimplementedAsyncRequest(server_, cq_);
    new UnimplementedAsyncResponse(this);
  } else {
    delete this;
  }
  return false;
}

Server::UnimplementedAsyncResponse::UnimplementedAsyncResponse(
    UnimplementedAsyncRequest* request)
    : request_(request) {
  Status status(StatusCode::UNIMPLEMENTED, "");
  UnknownMethodHandler::FillOps(request_->context(), this);
  request_->stream()->call_.PerformOps(this);
}

void Server::ScheduleCallback() {
  {
    grpc::unique_lock<grpc::mutex> lock(mu_);
    num_running_cb_++;
  }
  thread_pool_->Add(std::bind(&Server::RunRpc, this));
}

void Server::RunRpc() {
  // Wait for one more incoming rpc.
  bool ok;
  GPR_TIMER_SCOPE("Server::RunRpc", 0);
  auto* mrd = SyncRequest::Wait(&cq_, &ok);
  if (mrd) {
    ScheduleCallback();
    if (ok) {
      SyncRequest::CallData cd(this, mrd);
      {
        mrd->SetupRequest();
        grpc::unique_lock<grpc::mutex> lock(mu_);
        if (!shutdown_) {
          mrd->Request(server_, cq_.cq());
        } else {
          // destroy the structure that was created
          mrd->TeardownRequest();
        }
      }
      GPR_TIMER_SCOPE("cd.Run()", 0);
      cd.Run(global_callbacks_);
    }
  }

  {
    grpc::unique_lock<grpc::mutex> lock(mu_);
    num_running_cb_--;
    if (shutdown_) {
      callback_cv_.notify_all();
    }
  }
}

ServerInitializer* Server::initializer() { return server_initializer_.get(); }

}  // namespace grpc
