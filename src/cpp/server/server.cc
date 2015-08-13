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
        cq_(nullptr) {
    grpc_metadata_array_init(&request_metadata_);
  }

  ~SyncRequest() { grpc_metadata_array_destroy(&request_metadata_); }

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

  void SetupRequest() { cq_ = grpc_completion_queue_create(nullptr); }

  void TeardownRequest() {
    grpc_completion_queue_destroy(cq_);
    cq_ = nullptr;
  }

  void Request(grpc_server* server, grpc_completion_queue* notify_cq) {
    GPR_ASSERT(cq_ && !in_flight_);
    in_flight_ = true;
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

    void Run() {
      ctx_.BeginCompletionOp(&call_);
      method_->handler()->RunHandler(MethodHandler::HandlerParameter(
          &call_, &ctx_, request_payload_, call_.max_message_size()));
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
    return grpc_server_create(&args, nullptr);
  } else {
    return grpc_server_create(nullptr, nullptr);
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
  grpc_server_register_completion_queue(server_, cq_.cq(), nullptr);
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

bool Server::RegisterService(const grpc::string *host, RpcService* service) {
  for (int i = 0; i < service->GetMethodCount(); ++i) {
    RpcServiceMethod* method = service->GetMethod(i);
    void* tag = grpc_server_register_method(
        server_, method->name(), host ? host->c_str() : nullptr);
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

bool Server::RegisterAsyncService(const grpc::string *host, AsynchronousService* service) {
  GPR_ASSERT(service->server_ == nullptr &&
             "Can only register an asynchronous service against one server.");
  service->server_ = this;
  service->request_args_ = new void*[service->method_count_];
  for (size_t i = 0; i < service->method_count_; ++i) {
    void* tag = grpc_server_register_method(server_, service->method_names_[i],
                                            host ? host->c_str() : nullptr);
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
      m->SetupRequest();
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

void Server::PerformOpsOnCall(CallOpSetInterface* ops, Call* call) {
  static const size_t MAX_OPS = 8;
  size_t nops = 0;
  grpc_op cops[MAX_OPS];
  ops->FillOps(cops, &nops);
  auto result = grpc_call_start_batch(call->call(), cops, nops, ops, nullptr);
  GPR_ASSERT(GRPC_CALL_OK == result);
}

Server::BaseAsyncRequest::BaseAsyncRequest(
    Server* server, ServerContext* context,
    ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq, void* tag)
    : server_(server),
      context_(context),
      stream_(stream),
      call_cq_(call_cq),
      tag_(tag),
      call_(nullptr) {
  memset(&initial_metadata_array_, 0, sizeof(initial_metadata_array_));
}

Server::BaseAsyncRequest::~BaseAsyncRequest() {}

bool Server::BaseAsyncRequest::FinalizeResult(void** tag, bool* status) {
  if (*status) {
    for (size_t i = 0; i < initial_metadata_array_.count; i++) {
      context_->client_metadata_.insert(std::make_pair(
          grpc::string(initial_metadata_array_.metadata[i].key),
          grpc::string(initial_metadata_array_.metadata[i].value,
                       initial_metadata_array_.metadata[i].value +
                           initial_metadata_array_.metadata[i].value_length)));
    }
  }
  grpc_metadata_array_destroy(&initial_metadata_array_);
  context_->set_call(call_);
  context_->cq_ = call_cq_;
  Call call(call_, server_, call_cq_, server_->max_message_size_);
  if (*status && call_) {
    context_->BeginCompletionOp(&call);
  }
  // just the pointers inside call are copied here
  stream_->BindCall(&call);
  *tag = tag_;
  delete this;
  return true;
}

Server::RegisteredAsyncRequest::RegisteredAsyncRequest(
    Server* server, ServerContext* context,
    ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq, void* tag)
    : BaseAsyncRequest(server, context, stream, call_cq, tag) {}

void Server::RegisteredAsyncRequest::IssueRequest(
    void* registered_method, grpc_byte_buffer** payload,
    ServerCompletionQueue* notification_cq) {
  grpc_server_request_registered_call(
      server_->server_, registered_method, &call_, &context_->deadline_,
      &initial_metadata_array_, payload, call_cq_->cq(), notification_cq->cq(),
      this);
}

Server::GenericAsyncRequest::GenericAsyncRequest(
    Server* server, GenericServerContext* context,
    ServerAsyncStreamingInterface* stream, CompletionQueue* call_cq,
    ServerCompletionQueue* notification_cq, void* tag)
    : BaseAsyncRequest(server, context, stream, call_cq, tag) {
  grpc_call_details_init(&call_details_);
  GPR_ASSERT(notification_cq);
  GPR_ASSERT(call_cq);
  grpc_server_request_call(server->server_, &call_, &call_details_,
                           &initial_metadata_array_, call_cq->cq(),
                           notification_cq->cq(), this);
}

bool Server::GenericAsyncRequest::FinalizeResult(void** tag, bool* status) {
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
