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
#include <forward_list>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <grpc++/alarm.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/generic/generic_stub.h>
#include <grpc/grpc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/histogram.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/usage_timer.h"
#include "test/cpp/util/create_test_channel.h"

namespace grpc {
namespace testing {

class ClientRpcContext {
 public:
  ClientRpcContext() {}
  virtual ~ClientRpcContext() {}
  // next state, return false if done. Collect stats when appropriate
  virtual bool RunNextState(bool, Histogram* hist) = 0;
  virtual ClientRpcContext* StartNewClone() = 0;
  static void* tag(ClientRpcContext* c) { return reinterpret_cast<void*>(c); }
  static ClientRpcContext* detag(void* t) {
    return reinterpret_cast<ClientRpcContext*>(t);
  }

  virtual void Start(CompletionQueue* cq) = 0;
};

template <class RequestType, class ResponseType>
class ClientRpcContextUnaryImpl : public ClientRpcContext {
 public:
  ClientRpcContextUnaryImpl(
      BenchmarkService::Stub* stub, const RequestType& req,
      std::function<gpr_timespec()> next_issue,
      std::function<
          std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
              BenchmarkService::Stub*, grpc::ClientContext*, const RequestType&,
              CompletionQueue*)>
          start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::READY),
        callback_(on_done),
        next_issue_(next_issue),
        start_req_(start_req) {}
  ~ClientRpcContextUnaryImpl() GRPC_OVERRIDE {}
  void Start(CompletionQueue* cq) GRPC_OVERRIDE {
    cq_ = cq;
    if (!next_issue_) {  // ready to issue
      RunNextState(true, nullptr);
    } else {  // wait for the issue time
      alarm_.reset(new Alarm(cq_, next_issue_(), ClientRpcContext::tag(this)));
    }
  }
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    switch (next_state_) {
      case State::READY:
        start_ = UsageTimer::Now();
        response_reader_ = start_req_(stub_, &context_, req_, cq_);
        response_reader_->Finish(&response_, &status_,
                                 ClientRpcContext::tag(this));
        next_state_ = State::RESP_DONE;
        return true;
      case State::RESP_DONE:
        hist->Add((UsageTimer::Now() - start_) * 1e9);
        callback_(status_, &response_);
        next_state_ = State::INVALID;
        return false;
      default:
        GPR_ASSERT(false);
        return false;
    }
  }
  ClientRpcContext* StartNewClone() GRPC_OVERRIDE {
    return new ClientRpcContextUnaryImpl(stub_, req_, next_issue_, start_req_,
                                         callback_);
  }

 private:
  grpc::ClientContext context_;
  BenchmarkService::Stub* stub_;
  CompletionQueue* cq_;
  std::unique_ptr<Alarm> alarm_;
  RequestType req_;
  ResponseType response_;
  enum State { INVALID, READY, RESP_DONE };
  State next_state_;
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
      BenchmarkService::Stub*, grpc::ClientContext*, const RequestType&,
      CompletionQueue*)>
      start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      response_reader_;
};

typedef std::forward_list<ClientRpcContext*> context_list;

template <class StubType, class RequestType>
class AsyncClient : public ClientImpl<StubType, RequestType> {
  // Specify which protected members we are using since there is no
  // member name resolution until the template types are fully resolved
 public:
  using Client::SetupLoadTest;
  using Client::closed_loop_;
  using Client::NextIssuer;
  using ClientImpl<StubType, RequestType>::cores_;
  using ClientImpl<StubType, RequestType>::channels_;
  using ClientImpl<StubType, RequestType>::request_;
  AsyncClient(const ClientConfig& config,
              std::function<ClientRpcContext*(
                  StubType*, std::function<gpr_timespec()> next_issue,
                  const RequestType&)>
                  setup_ctx,
              std::function<std::unique_ptr<StubType>(std::shared_ptr<Channel>)>
                  create_stub)
      : ClientImpl<StubType, RequestType>(config, create_stub),
        num_async_threads_(NumThreads(config)) {
    SetupLoadTest(config, num_async_threads_);

    for (int i = 0; i < num_async_threads_; i++) {
      cli_cqs_.emplace_back(new CompletionQueue);
      next_issuers_.emplace_back(NextIssuer(i));
    }

    using namespace std::placeholders;
    int t = 0;
    for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
      for (int ch = 0; ch < config.client_channels(); ch++) {
        auto* cq = cli_cqs_[t].get();
        auto ctx =
            setup_ctx(channels_[ch].get_stub(), next_issuers_[t], request_);
        ctx->Start(cq);
        t = (t + 1) % cli_cqs_.size();
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

    if (cli_cqs_[thread_idx]->Next(&got_tag, &ok)) {
      // Got a regular event, so process it
      ClientRpcContext* ctx = ClientRpcContext::detag(got_tag);
      if (!ctx->RunNextState(ok, histogram)) {
        // The RPC and callback are done, so clone the ctx
        // and kickstart the new one
        auto clone = ctx->StartNewClone();
        clone->Start(cli_cqs_[thread_idx].get());
        // delete the old version
        delete ctx;
      }
      return true;
    } else {  // queue is shutting down
      return false;
    }
  }

 protected:
  const int num_async_threads_;

 private:
  int NumThreads(const ClientConfig& config) {
    int num_threads = config.async_client_threads();
    if (num_threads <= 0) {  // Use dynamic sizing
      num_threads = cores_;
      gpr_log(GPR_INFO, "Sizing async client to %d threads", num_threads);
    }
    return num_threads;
  }

  std::vector<std::unique_ptr<CompletionQueue>> cli_cqs_;
  std::vector<std::function<gpr_timespec()>> next_issuers_;
};

static std::unique_ptr<BenchmarkService::Stub> BenchmarkStubCreator(
    std::shared_ptr<Channel> ch) {
  return BenchmarkService::NewStub(ch);
}

class AsyncUnaryClient GRPC_FINAL
    : public AsyncClient<BenchmarkService::Stub, SimpleRequest> {
 public:
  explicit AsyncUnaryClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx, BenchmarkStubCreator) {
    StartThreads(num_async_threads_);
  }
  ~AsyncUnaryClient() GRPC_OVERRIDE { EndThreads(); }

 private:
  static void CheckDone(grpc::Status s, SimpleResponse* response) {}
  static std::unique_ptr<grpc::ClientAsyncResponseReader<SimpleResponse>>
  StartReq(BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
           const SimpleRequest& request, CompletionQueue* cq) {
    return stub->AsyncUnaryCall(ctx, request, cq);
  };
  static ClientRpcContext* SetupCtx(BenchmarkService::Stub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const SimpleRequest& req) {
    return new ClientRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
        stub, req, next_issue, AsyncUnaryClient::StartReq,
        AsyncUnaryClient::CheckDone);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingImpl(
      BenchmarkService::Stub* stub, const RequestType& req,
      std::function<gpr_timespec()> next_issue,
      std::function<std::unique_ptr<
          grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          BenchmarkService::Stub*, grpc::ClientContext*, CompletionQueue*,
          void*)>
          start_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::INVALID),
        callback_(on_done),
        next_issue_(next_issue),
        start_req_(start_req) {}
  ~ClientRpcContextStreamingImpl() GRPC_OVERRIDE {}
  void Start(CompletionQueue* cq) GRPC_OVERRIDE {
    cq_ = cq;
    stream_ = start_req_(stub_, &context_, cq, ClientRpcContext::tag(this));
    next_state_ = State::STREAM_IDLE;
  }
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    while (true) {
      switch (next_state_) {
        case State::STREAM_IDLE:
          if (!next_issue_) {  // ready to issue
            next_state_ = State::READY_TO_WRITE;
          } else {
            next_state_ = State::WAIT;
          }
          break;  // loop around, don't return
        case State::WAIT:
          alarm_.reset(
              new Alarm(cq_, next_issue_(), ClientRpcContext::tag(this)));
          next_state_ = State::READY_TO_WRITE;
          return true;
        case State::READY_TO_WRITE:
          if (!ok) {
            return false;
          }
          start_ = UsageTimer::Now();
          next_state_ = State::WRITE_DONE;
          stream_->Write(req_, ClientRpcContext::tag(this));
          return true;
        case State::WRITE_DONE:
          if (!ok) {
            return false;
          }
          next_state_ = State::READ_DONE;
          stream_->Read(&response_, ClientRpcContext::tag(this));
          return true;
          break;
        case State::READ_DONE:
          hist->Add((UsageTimer::Now() - start_) * 1e9);
          callback_(status_, &response_);
          next_state_ = State::STREAM_IDLE;
          break;  // loop around
        default:
          GPR_ASSERT(false);
          return false;
      }
    }
  }
  ClientRpcContext* StartNewClone() GRPC_OVERRIDE {
    return new ClientRpcContextStreamingImpl(stub_, req_, next_issue_,
                                             start_req_, callback_);
  }

 private:
  grpc::ClientContext context_;
  BenchmarkService::Stub* stub_;
  CompletionQueue* cq_;
  std::unique_ptr<Alarm> alarm_;
  RequestType req_;
  ResponseType response_;
  enum State {
    INVALID,
    STREAM_IDLE,
    WAIT,
    READY_TO_WRITE,
    WRITE_DONE,
    READ_DONE
  };
  State next_state_;
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<std::unique_ptr<
      grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
      BenchmarkService::Stub*, grpc::ClientContext*, CompletionQueue*, void*)>
      start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      stream_;
};

class AsyncStreamingClient GRPC_FINAL
    : public AsyncClient<BenchmarkService::Stub, SimpleRequest> {
 public:
  explicit AsyncStreamingClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx, BenchmarkStubCreator) {
    StartThreads(num_async_threads_);
  }

  ~AsyncStreamingClient() GRPC_OVERRIDE { EndThreads(); }

 private:
  static void CheckDone(grpc::Status s, SimpleResponse* response) {}
  static std::unique_ptr<
      grpc::ClientAsyncReaderWriter<SimpleRequest, SimpleResponse>>
  StartReq(BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
           CompletionQueue* cq, void* tag) {
    auto stream = stub->AsyncStreamingCall(ctx, cq, tag);
    return stream;
  };
  static ClientRpcContext* SetupCtx(BenchmarkService::Stub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const SimpleRequest& req) {
    return new ClientRpcContextStreamingImpl<SimpleRequest, SimpleResponse>(
        stub, req, next_issue, AsyncStreamingClient::StartReq,
        AsyncStreamingClient::CheckDone);
  }
};

class ClientRpcContextGenericStreamingImpl : public ClientRpcContext {
 public:
  ClientRpcContextGenericStreamingImpl(
      grpc::GenericStub* stub, const ByteBuffer& req,
      std::function<gpr_timespec()> next_issue,
      std::function<std::unique_ptr<grpc::GenericClientAsyncReaderWriter>(
          grpc::GenericStub*, grpc::ClientContext*,
          const grpc::string& method_name, CompletionQueue*, void*)>
          start_req,
      std::function<void(grpc::Status, ByteBuffer*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::INVALID),
        callback_(on_done),
        next_issue_(next_issue),
        start_req_(start_req) {}
  ~ClientRpcContextGenericStreamingImpl() GRPC_OVERRIDE {}
  void Start(CompletionQueue* cq) GRPC_OVERRIDE {
    cq_ = cq;
    const grpc::string kMethodName(
        "/grpc.testing.BenchmarkService/StreamingCall");
    stream_ = start_req_(stub_, &context_, kMethodName, cq,
                         ClientRpcContext::tag(this));
    next_state_ = State::STREAM_IDLE;
  }
  bool RunNextState(bool ok, Histogram* hist) GRPC_OVERRIDE {
    while (true) {
      switch (next_state_) {
        case State::STREAM_IDLE:
          if (!next_issue_) {  // ready to issue
            next_state_ = State::READY_TO_WRITE;
          } else {
            next_state_ = State::WAIT;
          }
          break;  // loop around, don't return
        case State::WAIT:
          alarm_.reset(
              new Alarm(cq_, next_issue_(), ClientRpcContext::tag(this)));
          next_state_ = State::READY_TO_WRITE;
          return true;
        case State::READY_TO_WRITE:
          if (!ok) {
            return false;
          }
          start_ = UsageTimer::Now();
          next_state_ = State::WRITE_DONE;
          stream_->Write(req_, ClientRpcContext::tag(this));
          return true;
        case State::WRITE_DONE:
          if (!ok) {
            return false;
          }
          next_state_ = State::READ_DONE;
          stream_->Read(&response_, ClientRpcContext::tag(this));
          return true;
          break;
        case State::READ_DONE:
          hist->Add((UsageTimer::Now() - start_) * 1e9);
          callback_(status_, &response_);
          next_state_ = State::STREAM_IDLE;
          break;  // loop around
        default:
          GPR_ASSERT(false);
          return false;
      }
    }
  }
  ClientRpcContext* StartNewClone() GRPC_OVERRIDE {
    return new ClientRpcContextGenericStreamingImpl(stub_, req_, next_issue_,
                                                    start_req_, callback_);
  }

 private:
  grpc::ClientContext context_;
  grpc::GenericStub* stub_;
  CompletionQueue* cq_;
  std::unique_ptr<Alarm> alarm_;
  ByteBuffer req_;
  ByteBuffer response_;
  enum State {
    INVALID,
    STREAM_IDLE,
    WAIT,
    READY_TO_WRITE,
    WRITE_DONE,
    READ_DONE
  };
  State next_state_;
  std::function<void(grpc::Status, ByteBuffer*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<std::unique_ptr<grpc::GenericClientAsyncReaderWriter>(
      grpc::GenericStub*, grpc::ClientContext*, const grpc::string&,
      CompletionQueue*, void*)>
      start_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::GenericClientAsyncReaderWriter> stream_;
};

static std::unique_ptr<grpc::GenericStub> GenericStubCreator(
    std::shared_ptr<Channel> ch) {
  return std::unique_ptr<grpc::GenericStub>(new grpc::GenericStub(ch));
}

class GenericAsyncStreamingClient GRPC_FINAL
    : public AsyncClient<grpc::GenericStub, ByteBuffer> {
 public:
  explicit GenericAsyncStreamingClient(const ClientConfig& config)
      : AsyncClient(config, SetupCtx, GenericStubCreator) {
    StartThreads(num_async_threads_);
  }

  ~GenericAsyncStreamingClient() GRPC_OVERRIDE { EndThreads(); }

 private:
  static void CheckDone(grpc::Status s, ByteBuffer* response) {}
  static std::unique_ptr<grpc::GenericClientAsyncReaderWriter> StartReq(
      grpc::GenericStub* stub, grpc::ClientContext* ctx,
      const grpc::string& method_name, CompletionQueue* cq, void* tag) {
    auto stream = stub->Call(ctx, method_name, cq, tag);
    return stream;
  };
  static ClientRpcContext* SetupCtx(grpc::GenericStub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const ByteBuffer& req) {
    return new ClientRpcContextGenericStreamingImpl(
        stub, req, next_issue, GenericAsyncStreamingClient::StartReq,
        GenericAsyncStreamingClient::CheckDone);
  }
};

std::unique_ptr<Client> CreateAsyncUnaryClient(const ClientConfig& args) {
  return std::unique_ptr<Client>(new AsyncUnaryClient(args));
}
std::unique_ptr<Client> CreateAsyncStreamingClient(const ClientConfig& args) {
  return std::unique_ptr<Client>(new AsyncStreamingClient(args));
}
std::unique_ptr<Client> CreateGenericAsyncStreamingClient(
    const ClientConfig& args) {
  return std::unique_ptr<Client>(new GenericAsyncStreamingClient(args));
}

}  // namespace testing
}  // namespace grpc
