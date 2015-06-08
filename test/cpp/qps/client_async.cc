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
#include <grpc++/stream.h>
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/qps/qpstest.grpc.pb.h"
#include "test/cpp/qps/timer.h"
#include "test/cpp/qps/client.h"

namespace grpc {
namespace testing {

class ClientRpcContext {
 public:
  ClientRpcContext() {}
  virtual ~ClientRpcContext() {}
  // next state, return false if done. Collect stats when appropriate
  virtual bool RunNextState(bool, Histogram* hist) = 0;
  virtual void StartNewClone() = 0;
  static void* tag(ClientRpcContext* c) { return reinterpret_cast<void*>(c); }
  static ClientRpcContext* detag(void* t) {
    return reinterpret_cast<ClientRpcContext*>(t);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextUnaryImpl : public ClientRpcContext {
 public:
  ClientRpcContextUnaryImpl(
      TestService::Stub* stub, const RequestType& req,
      std::function<
          std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
              TestService::Stub*, grpc::ClientContext*, const RequestType&)>
          start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        req_(req),
        response_(),
        next_state_(&ClientRpcContextUnaryImpl::RespDone),
        callback_(on_done),
        start_req_(start_req),
        start_(Timer::Now()),
        response_reader_(start_req(stub_, &context_, req_)) {
    response_reader_->Finish(&response_, &status_, ClientRpcContext::tag(this));
  }
  ~ClientRpcContextUnaryImpl() GRPC_OVERRIDE {}
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    bool ret = (this->*next_state_)(ok);
    if (!ret) {
      hist->Add((Timer::Now() - start_) * 1e9);
    }
    return ret;
  }

  void StartNewClone() GRPC_OVERRIDE {
    new ClientRpcContextUnaryImpl(stub_, req_, start_req_, callback_);
  }

 private:
  bool RespDone(bool) {
    next_state_ = &ClientRpcContextUnaryImpl::DoCallBack;
    return false;
  }
  bool DoCallBack(bool) {
    callback_(status_, &response_);
    return false;
  }
  grpc::ClientContext context_;
  TestService::Stub* stub_;
  RequestType req_;
  ResponseType response_;
  bool (ClientRpcContextUnaryImpl::*next_state_)(bool);
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
      TestService::Stub*, grpc::ClientContext*, const RequestType&)> start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      response_reader_;
};

class AsyncClient : public Client {
 public:
  explicit AsyncClient(const ClientConfig& config,
                       std::function<void(CompletionQueue*, TestService::Stub*,
                                          const SimpleRequest&)> setup_ctx)
      : Client(config) {
    for (int i = 0; i < config.async_client_threads(); i++) {
      cli_cqs_.emplace_back(new CompletionQueue);
    }
    int t = 0;
    for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
      for (auto channel = channels_.begin(); channel != channels_.end();
           channel++) {
        auto* cq = cli_cqs_[t].get();
        t = (t + 1) % cli_cqs_.size();
        setup_ctx(cq, channel->get_stub(), request_);
      }
    }
  }
  virtual ~AsyncClient() {
    for (auto cq = cli_cqs_.begin(); cq != cli_cqs_.end(); cq++) {
      (*cq)->Shutdown();
      void* got_tag;
      bool ok;
      while ((*cq)->Next(&got_tag, &ok)) {
        delete ClientRpcContext::detag(got_tag);
      }
    }
  }

  bool ThreadFunc(Histogram* histogram,
                  size_t thread_idx) GRPC_OVERRIDE GRPC_FINAL {
    void* got_tag;
    bool ok;
    switch (cli_cqs_[thread_idx]->AsyncNext(
        &got_tag, &ok,
        std::chrono::system_clock::now() + std::chrono::seconds(1))) {
      case CompletionQueue::SHUTDOWN:
        return false;
      case CompletionQueue::TIMEOUT:
        return true;
      case CompletionQueue::GOT_EVENT:
        break;
    }

    ClientRpcContext* ctx = ClientRpcContext::detag(got_tag);
    if (ctx->RunNextState(ok, histogram) == false) {
      // call the callback and then delete it
      ctx->RunNextState(ok, histogram);
      ctx->StartNewClone();
      delete ctx;
    }

    return true;
  }

 private:
  std::vector<std::unique_ptr<CompletionQueue>> cli_cqs_;
};

class AsyncUnaryClient GRPC_FINAL : public AsyncClient {
 public:
  explicit AsyncUnaryClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx) {
    StartThreads(config.async_client_threads());
  }
  ~AsyncUnaryClient() GRPC_OVERRIDE { EndThreads(); }

 private:
  static void SetupCtx(CompletionQueue* cq, TestService::Stub* stub,
                       const SimpleRequest& req) {
    auto check_done = [](grpc::Status s, SimpleResponse* response) {};
    auto start_req = [cq](TestService::Stub* stub, grpc::ClientContext* ctx,
                          const SimpleRequest& request) {
      return stub->AsyncUnaryCall(ctx, request, cq);
    };
    new ClientRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
        stub, req, start_req, check_done);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingImpl(
      TestService::Stub* stub, const RequestType& req,
      std::function<std::unique_ptr<
          grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          TestService::Stub*, grpc::ClientContext*, void*)> start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        req_(req),
        response_(),
        next_state_(&ClientRpcContextStreamingImpl::ReqSent),
        callback_(on_done),
        start_req_(start_req),
        start_(Timer::Now()),
        stream_(start_req_(stub_, &context_, ClientRpcContext::tag(this))) {}
  ~ClientRpcContextStreamingImpl() GRPC_OVERRIDE {}
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    return (this->*next_state_)(ok, hist);
  }
  void StartNewClone() GRPC_OVERRIDE {
    new ClientRpcContextStreamingImpl(stub_, req_, start_req_, callback_);
  }

 private:
  bool ReqSent(bool ok, Histogram*) { return StartWrite(ok); }
  bool StartWrite(bool ok) {
    if (!ok) {
      return (false);
    }
    start_ = Timer::Now();
    next_state_ = &ClientRpcContextStreamingImpl::WriteDone;
    stream_->Write(req_, ClientRpcContext::tag(this));
    return true;
  }
  bool WriteDone(bool ok, Histogram*) {
    if (!ok) {
      return (false);
    }
    next_state_ = &ClientRpcContextStreamingImpl::ReadDone;
    stream_->Read(&response_, ClientRpcContext::tag(this));
    return true;
  }
  bool ReadDone(bool ok, Histogram* hist) {
    hist->Add((Timer::Now() - start_) * 1e9);
    return StartWrite(ok);
  }
  grpc::ClientContext context_;
  TestService::Stub* stub_;
  RequestType req_;
  ResponseType response_;
  bool (ClientRpcContextStreamingImpl::*next_state_)(bool, Histogram*);
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          TestService::Stub*, grpc::ClientContext*, void*)> start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      stream_;
};

class AsyncStreamingClient GRPC_FINAL : public AsyncClient {
 public:
  explicit AsyncStreamingClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx) {
    StartThreads(config.async_client_threads());
  }

  ~AsyncStreamingClient() GRPC_OVERRIDE { EndThreads(); }

 private:
  static void SetupCtx(CompletionQueue* cq, TestService::Stub* stub,
                       const SimpleRequest& req) {
    auto check_done = [](grpc::Status s, SimpleResponse* response) {};
    auto start_req = [cq](TestService::Stub* stub, grpc::ClientContext* ctx,
                          void* tag) {
      auto stream = stub->AsyncStreamingCall(ctx, cq, tag);
      return stream;
    };
    new ClientRpcContextStreamingImpl<SimpleRequest, SimpleResponse>(
        stub, req, start_req, check_done);
  }
};

std::unique_ptr<Client> CreateAsyncUnaryClient(const ClientConfig& args) {
  return std::unique_ptr<Client>(new AsyncUnaryClient(args));
}
std::unique_ptr<Client> CreateAsyncStreamingClient(const ClientConfig& args) {
  return std::unique_ptr<Client>(new AsyncStreamingClient(args));
}

}  // namespace testing
}  // namespace grpc
