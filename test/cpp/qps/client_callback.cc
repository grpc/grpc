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

#include "src/proto/grpc/testing/benchmark_service.grpc.pb.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/usage_timer.h"

namespace grpc {
namespace testing {

/**
 * Maintains context info per RPC
 */
struct CallbackClientRpcContext {
  CallbackClientRpcContext(BenchmarkService::Stub* stub) : stub_(stub) {}

  ~CallbackClientRpcContext() {}

  SimpleResponse response_;
  ClientContext context_;
  Alarm alarm_;
  BenchmarkService::Stub* stub_;
};

static std::unique_ptr<BenchmarkService::Stub> BenchmarkStubCreator(
    const std::shared_ptr<Channel>& ch) {
  return BenchmarkService::NewStub(ch);
}

class CallbackClient
    : public ClientImpl<BenchmarkService::Stub, SimpleRequest> {
 public:
  CallbackClient(const ClientConfig& config)
      : ClientImpl<BenchmarkService::Stub, SimpleRequest>(
            config, BenchmarkStubCreator) {
    num_threads_ = NumThreads(config);
    rpcs_done_ = 0;
    SetupLoadTest(config, num_threads_);
    total_outstanding_rpcs_ =
        config.client_channels() * config.outstanding_rpcs_per_channel();
  }

  virtual ~CallbackClient() {}

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

  virtual void ScheduleRpc(Thread* t, size_t thread_idx,
                           size_t ctx_vector_idx) = 0;

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

 private:
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
  CallbackUnaryClient(const ClientConfig& config) : CallbackClient(config) {
    for (int ch = 0; ch < config.client_channels(); ch++) {
      for (int i = 0; i < config.outstanding_rpcs_per_channel(); i++) {
        ctx_.emplace_back(
            new CallbackClientRpcContext(channels_[ch].get_stub()));
      }
    }
    StartThreads(num_threads_);
  }
  ~CallbackUnaryClient() {}

 protected:
  bool ThreadFuncImpl(Thread* t, size_t thread_idx) override {
    for (size_t vector_idx = thread_idx; vector_idx < total_outstanding_rpcs_;
         vector_idx += num_threads_) {
      ScheduleRpc(t, thread_idx, vector_idx);
    }
    return true;
  }

  void InitThreadFuncImpl(size_t thread_idx) override { return; }

 private:
  void ScheduleRpc(Thread* t, size_t thread_idx, size_t vector_idx) override {
    if (!closed_loop_) {
      gpr_timespec next_issue_time = NextIssueTime(thread_idx);
      // Start an alarm callback to run the internal callback after
      // next_issue_time
      ctx_[vector_idx]->alarm_.experimental().Set(
          next_issue_time, [this, t, thread_idx, vector_idx](bool ok) {
            IssueUnaryCallbackRpc(t, thread_idx, vector_idx);
          });
    } else {
      IssueUnaryCallbackRpc(t, thread_idx, vector_idx);
    }
  }

  void IssueUnaryCallbackRpc(Thread* t, size_t thread_idx, size_t vector_idx) {
    GPR_TIMER_SCOPE("CallbackUnaryClient::ThreadFunc", 0);
    double start = UsageTimer::Now();
    ctx_[vector_idx]->stub_->experimental_async()->UnaryCall(
        (&ctx_[vector_idx]->context_), &request_, &ctx_[vector_idx]->response_,
        [this, t, thread_idx, start, vector_idx](grpc::Status s) {
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
            ctx_[vector_idx].reset(
                new CallbackClientRpcContext(ctx_[vector_idx]->stub_));
            // Schedule a new RPC
            ScheduleRpc(t, thread_idx, vector_idx);
          }
        });
  }
};

std::unique_ptr<Client> CreateCallbackClient(const ClientConfig& config) {
  switch (config.rpc_type()) {
    case UNARY:
      return std::unique_ptr<Client>(new CallbackUnaryClient(config));
    case STREAMING:
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
