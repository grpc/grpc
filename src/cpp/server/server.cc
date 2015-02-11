/*
 *
 * Copyright 2014, Google Inc.
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
#include <grpc/support/log.h>
#include <grpc++/completion_queue.h>
#include <grpc++/impl/rpc_service_method.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/thread_pool_interface.h>

#include "src/cpp/proto/proto_utils.h"

namespace grpc {

Server::Server(ThreadPoolInterface *thread_pool, bool thread_pool_owned,
               ServerCredentials *creds)
    : started_(false),
      shutdown_(false),
      num_running_cb_(0),
      thread_pool_(thread_pool),
      thread_pool_owned_(thread_pool_owned),
      secure_(creds != nullptr) {
  if (creds) {
    server_ = grpc_secure_server_create(creds->GetRawCreds(), cq_.cq(), nullptr);
  } else {
    server_ = grpc_server_create(cq_.cq(), nullptr);
  }
}

Server::Server() {
  // Should not be called.
  GPR_ASSERT(false);
}

Server::~Server() {
  std::unique_lock<std::mutex> lock(mu_);
  if (started_ && !shutdown_) {
    lock.unlock();
    Shutdown();
  } else {
    lock.unlock();
  }
  grpc_server_destroy(server_);
  if (thread_pool_owned_) {
    delete thread_pool_;
  }
}

bool Server::RegisterService(RpcService *service) {
  for (int i = 0; i < service->GetMethodCount(); ++i) {
    RpcServiceMethod *method = service->GetMethod(i);
    void *tag = grpc_server_register_method(server_, method->name(), nullptr);
    if (!tag) {
      gpr_log(GPR_DEBUG, "Attempt to register %s multiple times",
              method->name());
      return false;
    }
    methods_.emplace_back(method, tag);
  }
  return true;
}

int Server::AddPort(const grpc::string &addr) {
  GPR_ASSERT(!started_);
  if (secure_) {
    return grpc_server_add_secure_http2_port(server_, addr.c_str());
  } else {
    return grpc_server_add_http2_port(server_, addr.c_str());
  }
}

class Server::MethodRequestData final : public CompletionQueueTag {
 public:
  MethodRequestData(RpcServiceMethod *method, void *tag)
      : method_(method),
        tag_(tag),
        has_request_payload_(method->method_type() == RpcMethod::NORMAL_RPC ||
                             method->method_type() ==
                                 RpcMethod::SERVER_STREAMING),
        has_response_payload_(method->method_type() == RpcMethod::NORMAL_RPC ||
                              method->method_type() ==
                                  RpcMethod::CLIENT_STREAMING) {
    grpc_metadata_array_init(&request_metadata_);
  }

  static MethodRequestData *Wait(CompletionQueue *cq, bool *ok) {
    void *tag;
    if (!cq->Next(&tag, ok)) {
      return nullptr;
    }
    auto *mrd = static_cast<MethodRequestData *>(tag);
    GPR_ASSERT(mrd->in_flight_);
    return mrd;
  }

  void Request(grpc_server *server) {
    GPR_ASSERT(!in_flight_);
    in_flight_ = true;
    cq_ = grpc_completion_queue_create();
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_server_request_registered_call(
                   server, tag_, &call_, &deadline_, &request_metadata_,
                   has_request_payload_ ? &request_payload_ : nullptr, 
                   cq_, this));
  }

  void FinalizeResult(void **tag, bool *status) override {}

  class CallData {
   public:
    explicit CallData(Server *server, MethodRequestData *mrd)
        : cq_(mrd->cq_),
          call_(mrd->call_, server, &cq_),
          ctx_(mrd->deadline_, mrd->request_metadata_.metadata,
               mrd->request_metadata_.count),
          has_request_payload_(mrd->has_request_payload_),
          has_response_payload_(mrd->has_response_payload_),
          request_payload_(mrd->request_payload_),
          method_(mrd->method_) {
      GPR_ASSERT(mrd->in_flight_);
      mrd->in_flight_ = false;
      mrd->request_metadata_.count = 0;
    }

    void Run() {
      std::unique_ptr<google::protobuf::Message> req;
      std::unique_ptr<google::protobuf::Message> res;
      if (has_request_payload_) {
        req.reset(method_->AllocateRequestProto());
        if (!DeserializeProto(request_payload_, req.get())) {
          abort();  // for now
        }
      }
      if (has_response_payload_) {
        res.reset(method_->AllocateResponseProto());
      }
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
      GPR_ASSERT(cq_.Pluck(&buf));
    }

   private:
    CompletionQueue cq_;
    Call call_;
    ServerContext ctx_;
    const bool has_request_payload_;
    const bool has_response_payload_;
    grpc_byte_buffer *request_payload_;
    RpcServiceMethod *const method_;
  };

 private:
  RpcServiceMethod *const method_;
  void *const tag_;
  bool in_flight_ = false;
  const bool has_request_payload_;
  const bool has_response_payload_;
  grpc_call *call_;
  gpr_timespec deadline_;
  grpc_metadata_array request_metadata_;
  grpc_byte_buffer *request_payload_;
  grpc_completion_queue *cq_;
};

bool Server::Start() {
  GPR_ASSERT(!started_);
  started_ = true;
  grpc_server_start(server_);

  // Start processing rpcs.
  if (!methods_.empty()) {
    for (auto &m : methods_) {
      m.Request(server_);
    }

    ScheduleCallback();
  }

  return true;
}

void Server::Shutdown() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    if (started_ && !shutdown_) {
      shutdown_ = true;
      grpc_server_shutdown(server_);

      // Wait for running callbacks to finish.
      while (num_running_cb_ != 0) {
        callback_cv_.wait(lock);
      }
    }
  }
}

void Server::PerformOpsOnCall(CallOpBuffer *buf, Call *call) {
  static const size_t MAX_OPS = 8;
  size_t nops = MAX_OPS;
  grpc_op ops[MAX_OPS];
  buf->FillOps(ops, &nops);
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call->call(), ops, nops,
                                   buf));
}

void Server::ScheduleCallback() {
  {
    std::unique_lock<std::mutex> lock(mu_);
    num_running_cb_++;
  }
  thread_pool_->ScheduleCallback(std::bind(&Server::RunRpc, this));
}

void Server::RunRpc() {
  // Wait for one more incoming rpc.
  bool ok;
  auto *mrd = MethodRequestData::Wait(&cq_, &ok);
  if (mrd) {
    MethodRequestData::CallData cd(this, mrd);

    if (ok) {
      mrd->Request(server_);
      ScheduleCallback();

      cd.Run();
    }
  }

  {
    std::unique_lock<std::mutex> lock(mu_);
    num_running_cb_--;
    if (shutdown_) {
      callback_cv_.notify_all();
    }
  }
}

}  // namespace grpc
