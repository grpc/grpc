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

#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <sstream>

#include <grpc/grpc.h>
#include <grpc/support/histogram.h>
#include <grpc/support/log.h>
#include <gflags/gflags.h>
#include <grpc++/async_unary_call.h>
#include <grpc++/client_context.h>
#include <grpc++/status.h>
#include "test/core/util/grpc_profiler.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/qps/qpstest.pb.h"
#include "test/cpp/qps/timer.h"
#include "test/cpp/qps/client.h"

namespace grpc {
namespace testing {

class ClientRpcContext {
 public:
  ClientRpcContext() {}
  virtual ~ClientRpcContext() {}
  virtual bool RunNextState() = 0;  // do next state, return false if steps done
  virtual void StartNewClone() = 0;
  static void* tag(ClientRpcContext* c) { return reinterpret_cast<void*>(c); }
  static ClientRpcContext* detag(void* t) {
    return reinterpret_cast<ClientRpcContext*>(t);
  }
  virtual void report_stats(Histogram* hist) = 0;
};

template <class RequestType, class ResponseType>
class ClientRpcContextUnaryImpl : public ClientRpcContext {
 public:
  ClientRpcContextUnaryImpl(
      TestService::Stub* stub, const RequestType& req,
      std::function<
          std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
              TestService::Stub*, grpc::ClientContext*, const RequestType&,
              void*)> start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        req_(req),
        response_(),
        next_state_(&ClientRpcContextUnaryImpl::ReqSent),
        callback_(on_done),
        start_req_(start_req),
        start_(Timer::Now()),
        response_reader_(
            start_req(stub_, &context_, req_, ClientRpcContext::tag(this))) {}
  ~ClientRpcContextUnaryImpl() GRPC_OVERRIDE {}
  bool RunNextState() GRPC_OVERRIDE { return (this->*next_state_)(); }
  void report_stats(Histogram* hist) GRPC_OVERRIDE {
    hist->Add((Timer::Now() - start_) * 1e9);
  }

  void StartNewClone() GRPC_OVERRIDE {
    new ClientRpcContextUnaryImpl(stub_, req_, start_req_, callback_);
  }

 private:
  bool ReqSent() {
    next_state_ = &ClientRpcContextUnaryImpl::RespDone;
    response_reader_->Finish(&response_, &status_, ClientRpcContext::tag(this));
    return true;
  }
  bool RespDone() {
    next_state_ = &ClientRpcContextUnaryImpl::DoCallBack;
    return false;
  }
  bool DoCallBack() {
    callback_(status_, &response_);
    return false;
  }
  grpc::ClientContext context_;
  TestService::Stub* stub_;
  RequestType req_;
  ResponseType response_;
  bool (ClientRpcContextUnaryImpl::*next_state_)();
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
      TestService::Stub*, grpc::ClientContext*, const RequestType&, void*)>
      start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      response_reader_;
};

class AsyncClient GRPC_FINAL : public Client {
 public:
  explicit AsyncClient(const ClientConfig& config) : Client(config) {
    for (int i = 0; i < config.async_client_threads(); i++) {
      cli_cqs_.emplace_back(new CompletionQueue);
    }

    auto payload_size = config.payload_size();
    auto check_done = [payload_size](grpc::Status s, SimpleResponse* response) {
      GPR_ASSERT(s.IsOk() && (response->payload().type() ==
                              grpc::testing::PayloadType::COMPRESSABLE) &&
                 (response->payload().body().length() ==
                  static_cast<size_t>(payload_size)));
    };

    int t = 0;
    for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
      for (auto& channel : channels_) {
        auto* cq = cli_cqs_[t].get();
        t = (t + 1) % cli_cqs_.size();
        auto start_req = [cq](TestService::Stub* stub, grpc::ClientContext* ctx,
                              const SimpleRequest& request, void* tag) {
          return stub->AsyncUnaryCall(ctx, request, cq, tag);
        };

        TestService::Stub* stub = channel.get_stub();
        const SimpleRequest& request = request_;
        new ClientRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
            stub, request, start_req, check_done);
      }
    }

    StartThreads(config.async_client_threads());
  }

  ~AsyncClient() GRPC_OVERRIDE {
    EndThreads();

    for (auto& cq : cli_cqs_) {
      cq->Shutdown();
      void* got_tag;
      bool ok;
      while (cq->Next(&got_tag, &ok)) {
        delete ClientRpcContext::detag(got_tag);
      }
    }
  }

  void ThreadFunc(Histogram* histogram, size_t thread_idx) GRPC_OVERRIDE {
    void* got_tag;
    bool ok;
    cli_cqs_[thread_idx]->Next(&got_tag, &ok);

    ClientRpcContext* ctx = ClientRpcContext::detag(got_tag);
    if (ctx->RunNextState() == false) {
      // call the callback and then delete it
      ctx->report_stats(histogram);
      ctx->RunNextState();
      ctx->StartNewClone();
      delete ctx;
    }
  }

  std::vector<std::unique_ptr<CompletionQueue>> cli_cqs_;
};

std::unique_ptr<Client> CreateAsyncClient(const ClientConfig& args) {
  return std::unique_ptr<Client>(new AsyncClient(args));
}

}  // namespace testing
}  // namespace grpc
