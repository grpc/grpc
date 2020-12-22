/*
 *
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

#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <grpc/grpc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpcpp/alarm.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include "absl/memory/memory.h"

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/usage_timer.h"

namespace grpc {
namespace testing {

/**
 * Maintains context info per RPC
 */
struct CallbackClientRpcContext {
  explicit CallbackClientRpcContext(BenchmarkService::Stub* stub)
      : alarm_(nullptr), stub_(stub) {}

  ~CallbackClientRpcContext() {}

  SimpleResponse response_;
  ClientContext context_;
  std::unique_ptr<Alarm> alarm_;
  BenchmarkService::Stub* stub_;
};

static std::unique_ptr<BenchmarkService::Stub> BenchmarkStubCreator(
    const std::shared_ptr<Channel>& ch) {
  return BenchmarkService::NewStub(ch);
}

class CallbackClient
    : public ClientImpl<BenchmarkService::Stub, SimpleRequest> {
 public:
  explicit CallbackClient(const ClientConfig& config)
      : ClientImpl<BenchmarkService::Stub, SimpleRequest>(
            config, BenchmarkStubCreator) {
    num_threads_ = NumThreads(config);
    rpcs_done_ = 0;

    //  Don't divide the fixed load among threads as the user threads
    //  only bootstrap the RPCs
    SetupLoadTest(config, 1);
    total_outstanding_rpcs_ =
        config.client_channels() * config.outstanding_rpcs_per_channel();
  }

  ~CallbackClient() override {}

  /**
   * The main thread of the benchmark will be waiting on DestroyMultithreading.
   * Increment the rpcs_done_ variable to signify that the Callback RPC
   * after thread completion is done. When the last outstanding rpc increments
   * the counter it should also signal the main thread's conditional variable.
   */
  void NotifyMainThreadOfThreadCompletion() {
    std::lock_guard<std::mutex> l(shutdown_mu_);
    rpcs_done_++;
    if (rpcs_done_ == total_outstanding_rpcs_) {
      shutdown_cv_.notify_one();
    }
  }

  gpr_timespec NextRPCIssueTime() {
    std::lock_guard<std::mutex> l(next_issue_time_mu_);
    return Client::NextIssueTime(0);
  }

 protected:
  size_t num_threads_;
  size_t total_outstanding_rpcs_;
  // The below mutex and condition variable is used by main benchmark thread to
  // wait on completion of all RPCs before shutdown
  std::mutex shutdown_mu_;
  std::condition_variable shutdown_cv_;
  // Number of rpcs done after thread completion
  size_t rpcs_done_;
  // Vector of Context data pointers for running a RPC
  std::vector<std::unique_ptr<CallbackClientRpcContext>> ctx_;

  virtual void InitThreadFuncImpl(size_t thread_idx) = 0;
  virtual bool ThreadFuncImpl(Thread* t, size_t thread_idx) = 0;

  void ThreadFunc(size_t thread_idx, Thread* t) override {
    InitThreadFuncImpl(thread_idx);
    ThreadFuncImpl(t, thread_idx);
  }

 private:
  std::mutex next_issue_time_mu_;  // Used by next issue time

  int NumThreads(const ClientConfig& config) {
    int num_threads = config.async_client_threads();
    if (num_threads <= 0) {  // Use dynamic sizing
      num_threads = cores_;
      gpr_log(GPR_INFO, "Sizing callback client to %d threads", num_threads);
    }
    return num_threads;
  }

  /**
   * Wait until all outstanding Callback RPCs are done
   */
  void DestroyMultithreading() final {
    std::unique_lock<std::mutex> l(shutdown_mu_);
    while (rpcs_done_ != total_outstanding_rpcs_) {
      shutdown_cv_.wait(l);
    }
    EndThreads();
  }
};

class CallbackUnaryClient final : public CallbackClient {
 public:
  explicit CallbackUnaryClient(const ClientConfig& config)
      : CallbackClient(config) {
    for (int ch = 0; ch < config.client_channels(); ch++) {
      for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
        ctx_.emplace_back(
            new CallbackClientRpcContext(channels_[ch].get_stub()));
      }
    }
    StartThreads(num_threads_);
  }
  ~CallbackUnaryClient() override {}

 protected:
  bool ThreadFuncImpl(Thread* t, size_t thread_idx) override {
    for (size_t vector_idx = thread_idx; vector_idx < total_outstanding_rpcs_;
         vector_idx += num_threads_) {
      ScheduleRpc(t, vector_idx);
    }
    return true;
  }

  void InitThreadFuncImpl(size_t /*thread_idx*/) override {}

 private:
  void ScheduleRpc(Thread* t, size_t vector_idx) {
    if (!closed_loop_) {
      gpr_timespec next_issue_time = NextRPCIssueTime();
      // Start an alarm callback to run the internal callback after
      // next_issue_time
      if (ctx_[vector_idx]->alarm_ == nullptr) {
        ctx_[vector_idx]->alarm_ = absl::make_unique<Alarm>();
      }
      ctx_[vector_idx]->alarm_->experimental().Set(
          next_issue_time, [this, t, vector_idx](bool /*ok*/) {
            IssueUnaryCallbackRpc(t, vector_idx);
          });
    } else {
      IssueUnaryCallbackRpc(t, vector_idx);
    }
  }

  void IssueUnaryCallbackRpc(Thread* t, size_t vector_idx) {
    GPR_TIMER_SCOPE("CallbackUnaryClient::ThreadFunc", 0);
    double start = UsageTimer::Now();
    ctx_[vector_idx]->stub_->experimental_async()->UnaryCall(
        (&ctx_[vector_idx]->context_), &request_, &ctx_[vector_idx]->response_,
        [this, t, start, vector_idx](grpc::Status s) {
          // Update Histogram with data from the callback run
          HistogramEntry entry;
          if (s.ok()) {
            entry.set_value((UsageTimer::Now() - start) * 1e9);
          }
          entry.set_status(s.error_code());
          t->UpdateHistogram(&entry);

          if (ThreadCompleted() || !s.ok()) {
            // Notify thread of completion
            NotifyMainThreadOfThreadCompletion();
          } else {
            // Reallocate ctx for next RPC
            ctx_[vector_idx] = absl::make_unique<CallbackClientRpcContext>(
                ctx_[vector_idx]->stub_);
            // Schedule a new RPC
            ScheduleRpc(t, vector_idx);
          }
        });
  }
};

class CallbackStreamingClient : public CallbackClient {
 public:
  explicit CallbackStreamingClient(const ClientConfig& config)
      : CallbackClient(config),
        messages_per_stream_(config.messages_per_stream()) {
    for (int ch = 0; ch < config.client_channels(); ch++) {
      for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
        ctx_.emplace_back(
            new CallbackClientRpcContext(channels_[ch].get_stub()));
      }
    }
    StartThreads(num_threads_);
  }
  ~CallbackStreamingClient() override {}

  void AddHistogramEntry(double start, bool ok, Thread* thread_ptr) {
    // Update Histogram with data from the callback run
    HistogramEntry entry;
    if (ok) {
      entry.set_value((UsageTimer::Now() - start) * 1e9);
    }
    thread_ptr->UpdateHistogram(&entry);
  }

  int messages_per_stream() { return messages_per_stream_; }

 protected:
  const int messages_per_stream_;
};

class CallbackStreamingPingPongClient : public CallbackStreamingClient {
 public:
  explicit CallbackStreamingPingPongClient(const ClientConfig& config)
      : CallbackStreamingClient(config) {}
  ~CallbackStreamingPingPongClient() override {}
};

class CallbackStreamingPingPongReactor final
    : public grpc::experimental::ClientBidiReactor<SimpleRequest,
                                                   SimpleResponse> {
 public:
  CallbackStreamingPingPongReactor(
      CallbackStreamingPingPongClient* client,
      std::unique_ptr<CallbackClientRpcContext> ctx)
      : client_(client), ctx_(std::move(ctx)), messages_issued_(0) {}

  void StartNewRpc() {
    ctx_->stub_->experimental_async()->StreamingCall(&(ctx_->context_), this);
    write_time_ = UsageTimer::Now();
    StartWrite(client_->request());
    writes_done_started_.clear();
    StartCall();
  }

  void OnWriteDone(bool ok) override {
    if (!ok) {
      gpr_log(GPR_ERROR, "Error writing RPC");
    }
    if ((!ok || client_->ThreadCompleted()) &&
        !writes_done_started_.test_and_set()) {
      StartWritesDone();
    }
    StartRead(&ctx_->response_);
  }

  void OnReadDone(bool ok) override {
    client_->AddHistogramEntry(write_time_, ok, thread_ptr_);

    if (client_->ThreadCompleted() || !ok ||
        (client_->messages_per_stream() != 0 &&
         ++messages_issued_ >= client_->messages_per_stream())) {
      if (!ok) {
        gpr_log(GPR_ERROR, "Error reading RPC");
      }
      if (!writes_done_started_.test_and_set()) {
        StartWritesDone();
      }
      return;
    }
    if (!client_->IsClosedLoop()) {
      gpr_timespec next_issue_time = client_->NextRPCIssueTime();
      // Start an alarm callback to run the internal callback after
      // next_issue_time
      ctx_->alarm_->experimental().Set(next_issue_time, [this](bool /*ok*/) {
        write_time_ = UsageTimer::Now();
        StartWrite(client_->request());
      });
    } else {
      write_time_ = UsageTimer::Now();
      StartWrite(client_->request());
    }
  }

  void OnDone(const Status& s) override {
    if (client_->ThreadCompleted() || !s.ok()) {
      client_->NotifyMainThreadOfThreadCompletion();
      return;
    }
    ctx_ = absl::make_unique<CallbackClientRpcContext>(ctx_->stub_);
    ScheduleRpc();
  }

  void ScheduleRpc() {
    if (!client_->IsClosedLoop()) {
      gpr_timespec next_issue_time = client_->NextRPCIssueTime();
      // Start an alarm callback to run the internal callback after
      // next_issue_time
      if (ctx_->alarm_ == nullptr) {
        ctx_->alarm_ = absl::make_unique<Alarm>();
      }
      ctx_->alarm_->experimental().Set(next_issue_time,
                                       [this](bool /*ok*/) { StartNewRpc(); });
    } else {
      StartNewRpc();
    }
  }

  void set_thread_ptr(Client::Thread* ptr) { thread_ptr_ = ptr; }

  CallbackStreamingPingPongClient* client_;
  std::unique_ptr<CallbackClientRpcContext> ctx_;
  std::atomic_flag writes_done_started_;
  Client::Thread* thread_ptr_;  // Needed to update histogram entries
  double write_time_;           // Track ping-pong round start time
  int messages_issued_;         // Messages issued by this stream
};

class CallbackStreamingPingPongClientImpl final
    : public CallbackStreamingPingPongClient {
 public:
  explicit CallbackStreamingPingPongClientImpl(const ClientConfig& config)
      : CallbackStreamingPingPongClient(config) {
    for (size_t i = 0; i < total_outstanding_rpcs_; i++) {
      reactor_.emplace_back(
          new CallbackStreamingPingPongReactor(this, std::move(ctx_[i])));
    }
  }
  ~CallbackStreamingPingPongClientImpl() override {}

  bool ThreadFuncImpl(Client::Thread* t, size_t thread_idx) override {
    for (size_t vector_idx = thread_idx; vector_idx < total_outstanding_rpcs_;
         vector_idx += num_threads_) {
      reactor_[vector_idx]->set_thread_ptr(t);
      reactor_[vector_idx]->ScheduleRpc();
    }
    return true;
  }

  void InitThreadFuncImpl(size_t /*thread_idx*/) override {}

 private:
  std::vector<std::unique_ptr<CallbackStreamingPingPongReactor>> reactor_;
};

// TODO(mhaidry) : Implement Streaming from client, server and both ways

std::unique_ptr<Client> CreateCallbackClient(const ClientConfig& config) {
  switch (config.rpc_type()) {
    case UNARY:
      return std::unique_ptr<Client>(new CallbackUnaryClient(config));
    case STREAMING:
      return std::unique_ptr<Client>(
          new CallbackStreamingPingPongClientImpl(config));
    case STREAMING_FROM_CLIENT:
    case STREAMING_FROM_SERVER:
    case STREAMING_BOTH_WAYS:
      assert(false);
      return nullptr;
    default:
      assert(false);
      return nullptr;
  }
}

}  // namespace testing
}  // namespace grpc
