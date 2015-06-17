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
#include <utility>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc++/completion_queue.h>
#include <grpc++/async_generic_service.h>
#include <grpc++/impl/rpc_service_method.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/thread_pool_interface.h>
#include <grpc++/time.h>

#include "src/core/profiling/timers.h"
#include "src/cpp/proto/proto_utils.h"

namespace grpc {

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
        has_response_payload_(method->method_type() == RpcMethod::NORMAL_RPC ||
                              method->method_type() ==
                                  RpcMethod::CLIENT_STREAMING) {
    grpc_metadata_array_init(&request_metadata_);
  }

  ~SyncRequest() {
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

  void Request(grpc_server* server, grpc_completion_queue* notify_cq) {
    GPR_ASSERT(!in_flight_);
    in_flight_ = true;
    cq_ = grpc_completion_queue_create();
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_server_request_registered_call(
                   server, tag_, &call_, &deadline_, &request_metadata_,
                   has_request_payload_ ? &request_payload_ : nullptr, cq_,
                   notify_cq, this));
  }

  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
    if (!*status) {
      grpc_completion_queue_destroy(cq_);
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
          has_response_payload_(mrd->has_response_payload_),
          request_payload_(mrd->request_payload_),
          method_(mrd->method_) {
      ctx_.call_ = mrd->call_;
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

    void Run() {
      std::unique_ptr<grpc::protobuf::Message> req;
      std::unique_ptr<grpc::protobuf::Message> res;
      if (has_request_payload_) {
        GRPC_TIMER_BEGIN(GRPC_PTAG_PROTO_DESERIALIZE, call_.call());
        req.reset(method_->AllocateRequestProto());
        if (!DeserializeProto(request_payload_, req.get(),
                              call_.max_message_size())) {
          // FIXME(yangg) deal with deserialization failure
          cq_.Shutdown();
          return;
        }
        GRPC_TIMER_END(GRPC_PTAG_PROTO_DESERIALIZE, call_.call());
      }
      if (has_response_payload_) {
        res.reset(method_->AllocateResponseProto());
      }
      ctx_.BeginCompletionOp(&call_);
      auto status = method_->handler()->RunHandler(
          MethodHandler::HandlerParameter(&call_, &ctx_, req.get(), res.get()));
      CallOpBuffer buf;
      if (!ctx_.sent_initial_metadata_) {
        buf.AddSendInitialMetadata(&ctx_.initial_metadata_);
      }
      if (has_response_payload_) {
        buf.AddSendMessage(*res);
      }
      buf.AddServerSendStatus(&ctx_.trailing_metadata_, status);
      call_.PerformOps(&buf);
      cq_.Pluck(&buf);  /* status ignored */
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
    const bool has_response_payload_;
    grpc_byte_buffer* request_payload_;
    RpcServiceMethod* const method_;
  };

 private:
  RpcServiceMethod* const method_;
  void* const tag_;
  bool in_flight_;
  const bool has_request_payload_;
  const bool has_response_payload_;
  grpc_call* call_;
  gpr_timespec deadline_;
  grpc_metadata_array request_metadata_;
  grpc_byte_buffer* request_payload_;
  grpc_completion_queue* cq_;
};

static grpc_server* CreateServer(int max_message_size) {
  if (max_message_size > 0) {
    grpc_arg arg;
    arg.type = GRPC_ARG_INTEGER;
    arg.key = const_cast<char*>(GRPC_ARG_MAX_MESSAGE_LENGTH);
    arg.value.integer = max_message_size;
    grpc_channel_args args = {1, &arg};
    return grpc_server_create(&args);
  } else {
    return grpc_server_create(nullptr);
  }
}

Server::Server(ThreadPoolInterface* thread_pool, bool thread_pool_owned,
               int max_message_size)
    : max_message_size_(max_message_size),
      started_(false),
      shutdown_(false),
      num_running_cb_(0),
      sync_methods_(new std::list<SyncRequest>),
      server_(CreateServer(max_message_size)),
      thread_pool_(thread_pool),
      thread_pool_owned_(thread_pool_owned) {
  grpc_server_register_completion_queue(server_, cq_.cq());
}

Server::~Server() {
  {
    grpc::unique_lock<grpc::mutex> lock(mu_);
    if (started_ && !shutdown_) {
      lock.unlock();
      Shutdown();
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

bool Server::RegisterService(RpcService* service) {
  for (int i = 0; i < service->GetMethodCount(); ++i) {
    RpcServiceMethod* method = service->GetMethod(i);
    void* tag = grpc_server_register_method(server_, method->name(), nullptr);
    if (!tag) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times",
              method->name());
      return false;
    }
    SyncRequest request(method, tag);
    sync_methods_->emplace_back(request);
  }
  return true;
}

bool Server::RegisterAsyncService(AsynchronousService* service) {
  GPR_ASSERT(service->dispatch_impl_ == nullptr &&
             "Can only register an asynchronous service against one server.");
  service->dispatch_impl_ = this;
  service->request_args_ = new void*[service->method_count_];
  for (size_t i = 0; i < service->method_count_; ++i) {
    void* tag = grpc_server_register_method(server_, service->method_names_[i],
                                            nullptr);
    if (!tag) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times",
              service->method_names_[i]);
      return false;
    }
    service->request_args_[i] = tag;
  }
  return true;
}

void Server::RegisterAsyncGenericService(AsyncGenericService* service) {
  GPR_ASSERT(service->server_ == nullptr &&
             "Can only register an async generic service against one server.");
  service->server_ = this;
}

int Server::AddListeningPort(const grpc::string& addr,
                             ServerCredentials* creds) {
  GPR_ASSERT(!started_);
  return creds->AddPortToServer(addr, server_);
}

bool Server::Start() {
  GPR_ASSERT(!started_);
  started_ = true;
  grpc_server_start(server_);

  // Start processing rpcs.
  if (!sync_methods_->empty()) {
    for (auto m = sync_methods_->begin(); m != sync_methods_->end(); m++) {
      m->Request(server_, cq_.cq());
    }

    ScheduleCallback();
  }

  return true;
}

void Server::Shutdown() {
  grpc::unique_lock<grpc::mutex> lock(mu_);
  if (started_ && !shutdown_) {
    shutdown_ = true;
    grpc_server_shutdown_and_notify(server_, cq_.cq(), new ShutdownRequest());
    cq_.Shutdown();

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

void Server::PerformOpsOnCall(CallOpBuffer* buf, Call* call) {
  static const size_t MAX_OPS = 8;
  size_t nops = MAX_OPS;
  grpc_op ops[MAX_OPS];
  buf->FillOps(ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call->call(), ops, nops, buf));
}

class Server::AsyncRequest GRPC_FINAL : public CompletionQueueTag {
 public:
  AsyncRequest(Server* server, void* registered_method, ServerContext* ctx,
               grpc::protobuf::Message* request,
               ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
               ServerCompletionQueue* notification_cq, void* tag)
      : tag_(tag),
        request_(request),
        stream_(stream),
        call_cq_(call_cq),
        ctx_(ctx),
        generic_ctx_(nullptr),
        server_(server),
        call_(nullptr),
        payload_(nullptr) {
    memset(&array_, 0, sizeof(array_));
    grpc_call_details_init(&call_details_);
    GPR_ASSERT(notification_cq);
    GPR_ASSERT(call_cq);
    grpc_server_request_registered_call(
        server->server_, registered_method, &call_, &call_details_.deadline,
        &array_, request ? &payload_ : nullptr, call_cq->cq(),
        notification_cq->cq(), this);
  }

  AsyncRequest(Server* server, GenericServerContext* ctx,
               ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
               ServerCompletionQueue* notification_cq, void* tag)
      : tag_(tag),
        request_(nullptr),
        stream_(stream),
        call_cq_(call_cq),
        ctx_(nullptr),
        generic_ctx_(ctx),
        server_(server),
        call_(nullptr),
        payload_(nullptr) {
    memset(&array_, 0, sizeof(array_));
    grpc_call_details_init(&call_details_);
    GPR_ASSERT(notification_cq);
    GPR_ASSERT(call_cq);
    grpc_server_request_call(server->server_, &call_, &call_details_, &array_,
                             call_cq->cq(), notification_cq->cq(), this);
  }

  ~AsyncRequest() {
    if (payload_) {
      grpc_byte_buffer_destroy(payload_);
    }
    grpc_metadata_array_destroy(&array_);
  }

  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
    *tag = tag_;
    bool orig_status = *status;
    if (*status && request_) {
      if (payload_) {
        GRPC_TIMER_BEGIN(GRPC_PTAG_PROTO_DESERIALIZE, call_);
        *status =
            DeserializeProto(payload_, request_, server_->max_message_size_);
        GRPC_TIMER_END(GRPC_PTAG_PROTO_DESERIALIZE, call_);
      } else {
        *status = false;
      }
    }
    ServerContext* ctx = ctx_ ? ctx_ : generic_ctx_;
    GPR_ASSERT(ctx);
    if (*status) {
      ctx->deadline_ = call_details_.deadline;
      for (size_t i = 0; i < array_.count; i++) {
        ctx->client_metadata_.insert(std::make_pair(
            grpc::string(array_.metadata[i].key),
            grpc::string(
                array_.metadata[i].value,
                array_.metadata[i].value + array_.metadata[i].value_length)));
      }
      if (generic_ctx_) {
        // TODO(yangg) remove the copy here.
        generic_ctx_->method_ = call_details_.method;
        generic_ctx_->host_ = call_details_.host;
        gpr_free(call_details_.method);
        gpr_free(call_details_.host);
      }
    }
    ctx->call_ = call_;
    ctx->cq_ = call_cq_;
    Call call(call_, server_, call_cq_, server_->max_message_size_);
    if (orig_status && call_) {
      ctx->BeginCompletionOp(&call);
    }
    // just the pointers inside call are copied here
    stream_->BindCall(&call);
    delete this;
    return true;
  }

 private:
  void* const tag_;
  grpc::protobuf::Message* const request_;
  ServerAsyncStreamingInterface* const stream_;
  CompletionQueue* const call_cq_;
  ServerContext* const ctx_;
  GenericServerContext* const generic_ctx_;
  Server* const server_;
  grpc_call* call_;
  grpc_call_details call_details_;
  grpc_metadata_array array_;
  grpc_byte_buffer* payload_;
};

void Server::RequestAsyncCall(void* registered_method, ServerContext* context,
                              grpc::protobuf::Message* request,
                              ServerAsyncStreamingInterface* stream,
                              CompletionQueue* call_cq,
                              ServerCompletionQueue* notification_cq,
                              void* tag) {
  new AsyncRequest(this, registered_method, context, request, stream, call_cq,
                   notification_cq, tag);
}

void Server::RequestAsyncGenericCall(GenericServerContext* context,
                                     ServerAsyncStreamingInterface* stream,
                                     CompletionQueue* call_cq,
                                     ServerCompletionQueue* notification_cq,
                                     void* tag) {
  new AsyncRequest(this, context, stream, call_cq, notification_cq, tag);
}

void Server::ScheduleCallback() {
  {
    grpc::unique_lock<grpc::mutex> lock(mu_);
    num_running_cb_++;
  }
  thread_pool_->ScheduleCallback(std::bind(&Server::RunRpc, this));
}

void Server::RunRpc() {
  // Wait for one more incoming rpc.
  bool ok;
  auto* mrd = SyncRequest::Wait(&cq_, &ok);
  if (mrd) {
    ScheduleCallback();
    if (ok) {
      SyncRequest::CallData cd(this, mrd);
      {
        grpc::unique_lock<grpc::mutex> lock(mu_);
        if (!shutdown_) {
          mrd->Request(server_, cq_.cq());
        }
      }
      cd.Run();
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

}  // namespace grpc
