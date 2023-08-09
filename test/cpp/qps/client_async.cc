//
//
// Copyright 2015 gRPC authors.
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
//

#include <forward_list>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpcpp/alarm.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/generic/generic_stub.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
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
  virtual bool RunNextState(bool, HistogramEntry* entry) = 0;
  virtual void StartNewClone(CompletionQueue* cq) = 0;
  static void* tag(ClientRpcContext* c) { return static_cast<void*>(c); }
  static ClientRpcContext* detag(void* t) {
    return static_cast<ClientRpcContext*>(t);
  }

  virtual void Start(CompletionQueue* cq, const ClientConfig& config) = 0;
  virtual void TryCancel() = 0;
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
          prepare_req,
      std::function<void(grpc::Status, ResponseType*, HistogramEntry*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::READY),
        callback_(on_done),
        next_issue_(std::move(next_issue)),
        prepare_req_(prepare_req) {}
  ~ClientRpcContextUnaryImpl() override {}
  void Start(CompletionQueue* cq, const ClientConfig& config) override {
    GPR_ASSERT(!config.use_coalesce_api());  // not supported.
    StartInternal(cq);
  }
  bool RunNextState(bool /*ok*/, HistogramEntry* entry) override {
    switch (next_state_) {
      case State::READY:
        start_ = UsageTimer::Now();
        response_reader_ = prepare_req_(stub_, &context_, req_, cq_);
        response_reader_->StartCall();
        next_state_ = State::RESP_DONE;
        response_reader_->Finish(&response_, &status_,
                                 ClientRpcContext::tag(this));
        return true;
      case State::RESP_DONE:
        if (status_.ok()) {
          entry->set_value((UsageTimer::Now() - start_) * 1e9);
        }
        callback_(status_, &response_, entry);
        next_state_ = State::INVALID;
        return false;
      default:
        grpc_core::Crash("unreachable");
        return false;
    }
  }
  void StartNewClone(CompletionQueue* cq) override {
    auto* clone = new ClientRpcContextUnaryImpl(stub_, req_, next_issue_,
                                                prepare_req_, callback_);
    clone->StartInternal(cq);
  }
  void TryCancel() override { context_.TryCancel(); }

 private:
  grpc::ClientContext context_;
  BenchmarkService::Stub* stub_;
  CompletionQueue* cq_;
  std::unique_ptr<Alarm> alarm_;
  const RequestType& req_;
  ResponseType response_;
  enum State { INVALID, READY, RESP_DONE };
  State next_state_;
  std::function<void(grpc::Status, ResponseType*, HistogramEntry*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>(
      BenchmarkService::Stub*, grpc::ClientContext*, const RequestType&,
      CompletionQueue*)>
      prepare_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ResponseType>>
      response_reader_;

  void StartInternal(CompletionQueue* cq) {
    cq_ = cq;
    if (!next_issue_) {  // ready to issue
      RunNextState(true, nullptr);
    } else {  // wait for the issue time
      alarm_ = std::make_unique<Alarm>();
      alarm_->Set(cq_, next_issue_(), ClientRpcContext::tag(this));
    }
  }
};

template <class StubType, class RequestType>
class AsyncClient : public ClientImpl<StubType, RequestType> {
  // Specify which protected members we are using since there is no
  // member name resolution until the template types are fully resolved
 public:
  using Client::closed_loop_;
  using Client::NextIssuer;
  using Client::SetupLoadTest;
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

    int tpc = std::max(1, config.threads_per_cq());      // 1 if unspecified
    int num_cqs = (num_async_threads_ + tpc - 1) / tpc;  // ceiling operator
    for (int i = 0; i < num_cqs; i++) {
      cli_cqs_.emplace_back(new CompletionQueue);
    }

    for (int i = 0; i < num_async_threads_; i++) {
      cq_.emplace_back(i % cli_cqs_.size());
      next_issuers_.emplace_back(NextIssuer(i));
      shutdown_state_.emplace_back(new PerThreadShutdownState());
    }

    int t = 0;
    for (int ch = 0; ch < config.client_channels(); ch++) {
      for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
        auto* cq = cli_cqs_[t].get();
        auto ctx =
            setup_ctx(channels_[ch].get_stub(), next_issuers_[t], request_);
        ctx->Start(cq, config);
      }
      t = (t + 1) % cli_cqs_.size();
    }
  }
  ~AsyncClient() override {
    for (auto cq = cli_cqs_.begin(); cq != cli_cqs_.end(); cq++) {
      void* got_tag;
      bool ok;
      while ((*cq)->Next(&got_tag, &ok)) {
        delete ClientRpcContext::detag(got_tag);
      }
    }
  }

  int GetPollCount() override {
    int count = 0;
    for (auto cq = cli_cqs_.begin(); cq != cli_cqs_.end(); cq++) {
      count += grpc_get_cq_poll_num((*cq)->cq());
    }
    return count;
  }

 protected:
  const int num_async_threads_;

 private:
  struct PerThreadShutdownState {
    mutable std::mutex mutex;
    bool shutdown;
    PerThreadShutdownState() : shutdown(false) {}
  };

  int NumThreads(const ClientConfig& config) {
    int num_threads = config.async_client_threads();
    if (num_threads <= 0) {  // Use dynamic sizing
      num_threads = cores_;
      gpr_log(GPR_INFO, "Sizing async client to %d threads", num_threads);
    }
    return num_threads;
  }
  void DestroyMultithreading() final {
    for (auto ss = shutdown_state_.begin(); ss != shutdown_state_.end(); ++ss) {
      std::lock_guard<std::mutex> lock((*ss)->mutex);
      (*ss)->shutdown = true;
    }
    for (auto cq = cli_cqs_.begin(); cq != cli_cqs_.end(); cq++) {
      (*cq)->Shutdown();
    }
    this->EndThreads();  // this needed for resolution
  }

  ClientRpcContext* ProcessTag(size_t thread_idx, void* tag) {
    ClientRpcContext* ctx = ClientRpcContext::detag(tag);
    if (shutdown_state_[thread_idx]->shutdown) {
      ctx->TryCancel();
      delete ctx;
      bool ok;
      while (cli_cqs_[cq_[thread_idx]]->Next(&tag, &ok)) {
        ctx = ClientRpcContext::detag(tag);
        ctx->TryCancel();
        delete ctx;
      }
      return nullptr;
    }
    return ctx;
  }

  void ThreadFunc(size_t thread_idx, Client::Thread* t) final {
    void* got_tag;
    bool ok;

    HistogramEntry entry;
    HistogramEntry* entry_ptr = &entry;
    if (!cli_cqs_[cq_[thread_idx]]->Next(&got_tag, &ok)) {
      return;
    }
    std::mutex* shutdown_mu = &shutdown_state_[thread_idx]->mutex;
    shutdown_mu->lock();
    ClientRpcContext* ctx = ProcessTag(thread_idx, got_tag);
    if (ctx == nullptr) {
      shutdown_mu->unlock();
      return;
    }
    while (cli_cqs_[cq_[thread_idx]]->DoThenAsyncNext(
        [&, ctx, ok, entry_ptr, shutdown_mu]() {
          if (!ctx->RunNextState(ok, entry_ptr)) {
            // The RPC and callback are done, so clone the ctx
            // and kickstart the new one
            ctx->StartNewClone(cli_cqs_[cq_[thread_idx]].get());
            delete ctx;
          }
          shutdown_mu->unlock();
        },
        &got_tag, &ok, gpr_inf_future(GPR_CLOCK_REALTIME))) {
      t->UpdateHistogram(entry_ptr);
      entry = HistogramEntry();
      shutdown_mu->lock();
      ctx = ProcessTag(thread_idx, got_tag);
      if (ctx == nullptr) {
        shutdown_mu->unlock();
        return;
      }
    }
  }

  std::vector<std::unique_ptr<CompletionQueue>> cli_cqs_;
  std::vector<int> cq_;
  std::vector<std::function<gpr_timespec()>> next_issuers_;
  std::vector<std::unique_ptr<PerThreadShutdownState>> shutdown_state_;
};

static std::unique_ptr<BenchmarkService::Stub> BenchmarkStubCreator(
    const std::shared_ptr<Channel>& ch) {
  return BenchmarkService::NewStub(ch);
}

class AsyncUnaryClient final
    : public AsyncClient<BenchmarkService::Stub, SimpleRequest> {
 public:
  explicit AsyncUnaryClient(const ClientConfig& config)
      : AsyncClient<BenchmarkService::Stub, SimpleRequest>(
            config, SetupCtx, BenchmarkStubCreator) {
    StartThreads(num_async_threads_);
  }
  ~AsyncUnaryClient() override {}

 private:
  static void CheckDone(const grpc::Status& s, SimpleResponse* /*response*/,
                        HistogramEntry* entry) {
    entry->set_status(s.error_code());
  }
  static std::unique_ptr<grpc::ClientAsyncResponseReader<SimpleResponse>>
  PrepareReq(BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
             const SimpleRequest& request, CompletionQueue* cq) {
    return stub->PrepareAsyncUnaryCall(ctx, request, cq);
  };
  static ClientRpcContext* SetupCtx(BenchmarkService::Stub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const SimpleRequest& req) {
    return new ClientRpcContextUnaryImpl<SimpleRequest, SimpleResponse>(
        stub, req, std::move(next_issue), AsyncUnaryClient::PrepareReq,
        AsyncUnaryClient::CheckDone);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingPingPongImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingPingPongImpl(
      BenchmarkService::Stub* stub, const RequestType& req,
      std::function<gpr_timespec()> next_issue,
      std::function<std::unique_ptr<
          grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          BenchmarkService::Stub*, grpc::ClientContext*, CompletionQueue*)>
          prepare_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::INVALID),
        callback_(on_done),
        next_issue_(std::move(next_issue)),
        prepare_req_(prepare_req),
        coalesce_(false) {}
  ~ClientRpcContextStreamingPingPongImpl() override {}
  void Start(CompletionQueue* cq, const ClientConfig& config) override {
    StartInternal(cq, config.messages_per_stream(), config.use_coalesce_api());
  }
  bool RunNextState(bool ok, HistogramEntry* entry) override {
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
          next_state_ = State::READY_TO_WRITE;
          alarm_ = std::make_unique<Alarm>();
          alarm_->Set(cq_, next_issue_(), ClientRpcContext::tag(this));
          return true;
        case State::READY_TO_WRITE:
          if (!ok) {
            return false;
          }
          start_ = UsageTimer::Now();
          next_state_ = State::WRITE_DONE;
          if (coalesce_ && messages_issued_ == messages_per_stream_ - 1) {
            stream_->WriteLast(req_, WriteOptions(),
                               ClientRpcContext::tag(this));
          } else {
            stream_->Write(req_, ClientRpcContext::tag(this));
          }
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
          entry->set_value((UsageTimer::Now() - start_) * 1e9);
          callback_(status_, &response_);
          if ((messages_per_stream_ != 0) &&
              (++messages_issued_ >= messages_per_stream_)) {
            next_state_ = State::WRITES_DONE_DONE;
            if (coalesce_) {
              // WritesDone should have been called on the last Write.
              // loop around to call Finish.
              break;
            }
            stream_->WritesDone(ClientRpcContext::tag(this));
            return true;
          }
          next_state_ = State::STREAM_IDLE;
          break;  // loop around
        case State::WRITES_DONE_DONE:
          next_state_ = State::FINISH_DONE;
          stream_->Finish(&status_, ClientRpcContext::tag(this));
          return true;
        case State::FINISH_DONE:
          next_state_ = State::INVALID;
          return false;
          break;
        default:
          grpc_core::Crash("unreachable");
          return false;
      }
    }
  }
  void StartNewClone(CompletionQueue* cq) override {
    auto* clone = new ClientRpcContextStreamingPingPongImpl(
        stub_, req_, next_issue_, prepare_req_, callback_);
    clone->StartInternal(cq, messages_per_stream_, coalesce_);
  }
  void TryCancel() override { context_.TryCancel(); }

 private:
  grpc::ClientContext context_;
  BenchmarkService::Stub* stub_;
  CompletionQueue* cq_;
  std::unique_ptr<Alarm> alarm_;
  const RequestType& req_;
  ResponseType response_;
  enum State {
    INVALID,
    STREAM_IDLE,
    WAIT,
    READY_TO_WRITE,
    WRITE_DONE,
    READ_DONE,
    WRITES_DONE_DONE,
    FINISH_DONE
  };
  State next_state_;
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<
      std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>(
          BenchmarkService::Stub*, grpc::ClientContext*, CompletionQueue*)>
      prepare_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncReaderWriter<RequestType, ResponseType>>
      stream_;

  // Allow a limit on number of messages in a stream
  int messages_per_stream_;
  int messages_issued_;
  // Whether to use coalescing API.
  bool coalesce_;

  void StartInternal(CompletionQueue* cq, int messages_per_stream,
                     bool coalesce) {
    cq_ = cq;
    messages_per_stream_ = messages_per_stream;
    messages_issued_ = 0;
    coalesce_ = coalesce;
    if (coalesce_) {
      GPR_ASSERT(messages_per_stream_ != 0);
      context_.set_initial_metadata_corked(true);
    }
    stream_ = prepare_req_(stub_, &context_, cq);
    next_state_ = State::STREAM_IDLE;
    stream_->StartCall(ClientRpcContext::tag(this));
    if (coalesce_) {
      // When the initial metadata is corked, the tag will not come back and we
      // need to manually drive the state machine.
      RunNextState(true, nullptr);
    }
  }
};

class AsyncStreamingPingPongClient final
    : public AsyncClient<BenchmarkService::Stub, SimpleRequest> {
 public:
  explicit AsyncStreamingPingPongClient(const ClientConfig& config)
      : AsyncClient<BenchmarkService::Stub, SimpleRequest>(
            config, SetupCtx, BenchmarkStubCreator) {
    StartThreads(num_async_threads_);
  }

  ~AsyncStreamingPingPongClient() override {}

 private:
  static void CheckDone(const grpc::Status& /*s*/,
                        SimpleResponse* /*response*/) {}
  static std::unique_ptr<
      grpc::ClientAsyncReaderWriter<SimpleRequest, SimpleResponse>>
  PrepareReq(BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
             CompletionQueue* cq) {
    auto stream = stub->PrepareAsyncStreamingCall(ctx, cq);
    return stream;
  };
  static ClientRpcContext* SetupCtx(BenchmarkService::Stub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const SimpleRequest& req) {
    return new ClientRpcContextStreamingPingPongImpl<SimpleRequest,
                                                     SimpleResponse>(
        stub, req, std::move(next_issue),
        AsyncStreamingPingPongClient::PrepareReq,
        AsyncStreamingPingPongClient::CheckDone);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingFromClientImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingFromClientImpl(
      BenchmarkService::Stub* stub, const RequestType& req,
      std::function<gpr_timespec()> next_issue,
      std::function<std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>(
          BenchmarkService::Stub*, grpc::ClientContext*, ResponseType*,
          CompletionQueue*)>
          prepare_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::INVALID),
        callback_(on_done),
        next_issue_(std::move(next_issue)),
        prepare_req_(prepare_req) {}
  ~ClientRpcContextStreamingFromClientImpl() override {}
  void Start(CompletionQueue* cq, const ClientConfig& config) override {
    GPR_ASSERT(!config.use_coalesce_api());  // not supported yet.
    StartInternal(cq);
  }
  bool RunNextState(bool ok, HistogramEntry* entry) override {
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
          alarm_ = std::make_unique<Alarm>();
          alarm_->Set(cq_, next_issue_(), ClientRpcContext::tag(this));
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
          entry->set_value((UsageTimer::Now() - start_) * 1e9);
          next_state_ = State::STREAM_IDLE;
          break;  // loop around
        default:
          grpc_core::Crash("unreachable");
          return false;
      }
    }
  }
  void StartNewClone(CompletionQueue* cq) override {
    auto* clone = new ClientRpcContextStreamingFromClientImpl(
        stub_, req_, next_issue_, prepare_req_, callback_);
    clone->StartInternal(cq);
  }
  void TryCancel() override { context_.TryCancel(); }

 private:
  grpc::ClientContext context_;
  BenchmarkService::Stub* stub_;
  CompletionQueue* cq_;
  std::unique_ptr<Alarm> alarm_;
  const RequestType& req_;
  ResponseType response_;
  enum State {
    INVALID,
    STREAM_IDLE,
    WAIT,
    READY_TO_WRITE,
    WRITE_DONE,
  };
  State next_state_;
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<std::unique_ptr<grpc::ClientAsyncWriter<RequestType>>(
      BenchmarkService::Stub*, grpc::ClientContext*, ResponseType*,
      CompletionQueue*)>
      prepare_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncWriter<RequestType>> stream_;

  void StartInternal(CompletionQueue* cq) {
    cq_ = cq;
    stream_ = prepare_req_(stub_, &context_, &response_, cq);
    next_state_ = State::STREAM_IDLE;
    stream_->StartCall(ClientRpcContext::tag(this));
  }
};

class AsyncStreamingFromClientClient final
    : public AsyncClient<BenchmarkService::Stub, SimpleRequest> {
 public:
  explicit AsyncStreamingFromClientClient(const ClientConfig& config)
      : AsyncClient<BenchmarkService::Stub, SimpleRequest>(
            config, SetupCtx, BenchmarkStubCreator) {
    StartThreads(num_async_threads_);
  }

  ~AsyncStreamingFromClientClient() override {}

 private:
  static void CheckDone(const grpc::Status& /*s*/,
                        SimpleResponse* /*response*/) {}
  static std::unique_ptr<grpc::ClientAsyncWriter<SimpleRequest>> PrepareReq(
      BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
      SimpleResponse* resp, CompletionQueue* cq) {
    auto stream = stub->PrepareAsyncStreamingFromClient(ctx, resp, cq);
    return stream;
  };
  static ClientRpcContext* SetupCtx(BenchmarkService::Stub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const SimpleRequest& req) {
    return new ClientRpcContextStreamingFromClientImpl<SimpleRequest,
                                                       SimpleResponse>(
        stub, req, std::move(next_issue),
        AsyncStreamingFromClientClient::PrepareReq,
        AsyncStreamingFromClientClient::CheckDone);
  }
};

template <class RequestType, class ResponseType>
class ClientRpcContextStreamingFromServerImpl : public ClientRpcContext {
 public:
  ClientRpcContextStreamingFromServerImpl(
      BenchmarkService::Stub* stub, const RequestType& req,
      std::function<gpr_timespec()> next_issue,
      std::function<std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>(
          BenchmarkService::Stub*, grpc::ClientContext*, const RequestType&,
          CompletionQueue*)>
          prepare_req,
      std::function<void(grpc::Status, ResponseType*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::INVALID),
        callback_(on_done),
        next_issue_(std::move(next_issue)),
        prepare_req_(prepare_req) {}
  ~ClientRpcContextStreamingFromServerImpl() override {}
  void Start(CompletionQueue* cq, const ClientConfig& config) override {
    GPR_ASSERT(!config.use_coalesce_api());  // not supported
    StartInternal(cq);
  }
  bool RunNextState(bool ok, HistogramEntry* entry) override {
    while (true) {
      switch (next_state_) {
        case State::STREAM_IDLE:
          if (!ok) {
            return false;
          }
          start_ = UsageTimer::Now();
          next_state_ = State::READ_DONE;
          stream_->Read(&response_, ClientRpcContext::tag(this));
          return true;
        case State::READ_DONE:
          if (!ok) {
            return false;
          }
          entry->set_value((UsageTimer::Now() - start_) * 1e9);
          callback_(status_, &response_);
          next_state_ = State::STREAM_IDLE;
          break;  // loop around
        default:
          grpc_core::Crash("unreachable");
          return false;
      }
    }
  }
  void StartNewClone(CompletionQueue* cq) override {
    auto* clone = new ClientRpcContextStreamingFromServerImpl(
        stub_, req_, next_issue_, prepare_req_, callback_);
    clone->StartInternal(cq);
  }
  void TryCancel() override { context_.TryCancel(); }

 private:
  grpc::ClientContext context_;
  BenchmarkService::Stub* stub_;
  CompletionQueue* cq_;
  std::unique_ptr<Alarm> alarm_;
  const RequestType& req_;
  ResponseType response_;
  enum State { INVALID, STREAM_IDLE, READ_DONE };
  State next_state_;
  std::function<void(grpc::Status, ResponseType*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<std::unique_ptr<grpc::ClientAsyncReader<ResponseType>>(
      BenchmarkService::Stub*, grpc::ClientContext*, const RequestType&,
      CompletionQueue*)>
      prepare_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::ClientAsyncReader<ResponseType>> stream_;

  void StartInternal(CompletionQueue* cq) {
    // TODO(vjpai): Add support to rate-pace this
    cq_ = cq;
    stream_ = prepare_req_(stub_, &context_, req_, cq);
    next_state_ = State::STREAM_IDLE;
    stream_->StartCall(ClientRpcContext::tag(this));
  }
};

class AsyncStreamingFromServerClient final
    : public AsyncClient<BenchmarkService::Stub, SimpleRequest> {
 public:
  explicit AsyncStreamingFromServerClient(const ClientConfig& config)
      : AsyncClient<BenchmarkService::Stub, SimpleRequest>(
            config, SetupCtx, BenchmarkStubCreator) {
    StartThreads(num_async_threads_);
  }

  ~AsyncStreamingFromServerClient() override {}

 private:
  static void CheckDone(const grpc::Status& /*s*/,
                        SimpleResponse* /*response*/) {}
  static std::unique_ptr<grpc::ClientAsyncReader<SimpleResponse>> PrepareReq(
      BenchmarkService::Stub* stub, grpc::ClientContext* ctx,
      const SimpleRequest& req, CompletionQueue* cq) {
    auto stream = stub->PrepareAsyncStreamingFromServer(ctx, req, cq);
    return stream;
  };
  static ClientRpcContext* SetupCtx(BenchmarkService::Stub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const SimpleRequest& req) {
    return new ClientRpcContextStreamingFromServerImpl<SimpleRequest,
                                                       SimpleResponse>(
        stub, req, std::move(next_issue),
        AsyncStreamingFromServerClient::PrepareReq,
        AsyncStreamingFromServerClient::CheckDone);
  }
};

class ClientRpcContextGenericStreamingImpl : public ClientRpcContext {
 public:
  ClientRpcContextGenericStreamingImpl(
      grpc::GenericStub* stub, const ByteBuffer& req,
      std::function<gpr_timespec()> next_issue,
      std::function<std::unique_ptr<grpc::GenericClientAsyncReaderWriter>(
          grpc::GenericStub*, grpc::ClientContext*,
          const std::string& method_name, CompletionQueue*)>
          prepare_req,
      std::function<void(grpc::Status, ByteBuffer*)> on_done)
      : context_(),
        stub_(stub),
        cq_(nullptr),
        req_(req),
        response_(),
        next_state_(State::INVALID),
        callback_(std::move(on_done)),
        next_issue_(std::move(next_issue)),
        prepare_req_(std::move(prepare_req)) {}
  ~ClientRpcContextGenericStreamingImpl() override {}
  void Start(CompletionQueue* cq, const ClientConfig& config) override {
    GPR_ASSERT(!config.use_coalesce_api());  // not supported yet.
    StartInternal(cq, config.messages_per_stream());
  }
  bool RunNextState(bool ok, HistogramEntry* entry) override {
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
          next_state_ = State::READY_TO_WRITE;
          alarm_ = std::make_unique<Alarm>();
          alarm_->Set(cq_, next_issue_(), ClientRpcContext::tag(this));
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
        case State::READ_DONE:
          entry->set_value((UsageTimer::Now() - start_) * 1e9);
          callback_(status_, &response_);
          if ((messages_per_stream_ != 0) &&
              (++messages_issued_ >= messages_per_stream_)) {
            next_state_ = State::WRITES_DONE_DONE;
            stream_->WritesDone(ClientRpcContext::tag(this));
            return true;
          }
          next_state_ = State::STREAM_IDLE;
          break;  // loop around
        case State::WRITES_DONE_DONE:
          next_state_ = State::FINISH_DONE;
          stream_->Finish(&status_, ClientRpcContext::tag(this));
          return true;
        case State::FINISH_DONE:
          next_state_ = State::INVALID;
          return false;
        default:
          grpc_core::Crash("unreachable");
          return false;
      }
    }
  }
  void StartNewClone(CompletionQueue* cq) override {
    auto* clone = new ClientRpcContextGenericStreamingImpl(
        stub_, req_, next_issue_, prepare_req_, callback_);
    clone->StartInternal(cq, messages_per_stream_);
  }
  void TryCancel() override { context_.TryCancel(); }

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
    READ_DONE,
    WRITES_DONE_DONE,
    FINISH_DONE
  };
  State next_state_;
  std::function<void(grpc::Status, ByteBuffer*)> callback_;
  std::function<gpr_timespec()> next_issue_;
  std::function<std::unique_ptr<grpc::GenericClientAsyncReaderWriter>(
      grpc::GenericStub*, grpc::ClientContext*, const std::string&,
      CompletionQueue*)>
      prepare_req_;
  grpc::Status status_;
  double start_;
  std::unique_ptr<grpc::GenericClientAsyncReaderWriter> stream_;

  // Allow a limit on number of messages in a stream
  int messages_per_stream_;
  int messages_issued_;

  void StartInternal(CompletionQueue* cq, int messages_per_stream) {
    cq_ = cq;
    const std::string kMethodName(
        "/grpc.testing.BenchmarkService/StreamingCall");
    messages_per_stream_ = messages_per_stream;
    messages_issued_ = 0;
    stream_ = prepare_req_(stub_, &context_, kMethodName, cq);
    next_state_ = State::STREAM_IDLE;
    stream_->StartCall(ClientRpcContext::tag(this));
  }
};

static std::unique_ptr<grpc::GenericStub> GenericStubCreator(
    const std::shared_ptr<Channel>& ch) {
  return std::make_unique<grpc::GenericStub>(ch);
}

class GenericAsyncStreamingClient final
    : public AsyncClient<grpc::GenericStub, ByteBuffer> {
 public:
  explicit GenericAsyncStreamingClient(const ClientConfig& config)
      : AsyncClient<grpc::GenericStub, ByteBuffer>(config, SetupCtx,
                                                   GenericStubCreator) {
    StartThreads(num_async_threads_);
  }

  ~GenericAsyncStreamingClient() override {}

 private:
  static void CheckDone(const grpc::Status& /*s*/, ByteBuffer* /*response*/) {}
  static std::unique_ptr<grpc::GenericClientAsyncReaderWriter> PrepareReq(
      grpc::GenericStub* stub, grpc::ClientContext* ctx,
      const std::string& method_name, CompletionQueue* cq) {
    auto stream = stub->PrepareCall(ctx, method_name, cq);
    return stream;
  };
  static ClientRpcContext* SetupCtx(grpc::GenericStub* stub,
                                    std::function<gpr_timespec()> next_issue,
                                    const ByteBuffer& req) {
    return new ClientRpcContextGenericStreamingImpl(
        stub, req, std::move(next_issue),
        GenericAsyncStreamingClient::PrepareReq,
        GenericAsyncStreamingClient::CheckDone);
  }
};

std::unique_ptr<Client> CreateAsyncClient(const ClientConfig& config) {
  switch (config.rpc_type()) {
    case UNARY:
      return std::unique_ptr<Client>(new AsyncUnaryClient(config));
    case STREAMING:
      return std::unique_ptr<Client>(new AsyncStreamingPingPongClient(config));
    case STREAMING_FROM_CLIENT:
      return std::unique_ptr<Client>(
          new AsyncStreamingFromClientClient(config));
    case STREAMING_FROM_SERVER:
      return std::unique_ptr<Client>(
          new AsyncStreamingFromServerClient(config));
    case STREAMING_BOTH_WAYS:
      // TODO(vjpai): Implement this
      assert(false);
      return nullptr;
    default:
      assert(false);
      return nullptr;
  }
}
std::unique_ptr<Client> CreateGenericAsyncStreamingClient(
    const ClientConfig& config) {
  return std::unique_ptr<Client>(new GenericAsyncStreamingClient(config));
}

}  // namespace testing
}  // namespace grpc
