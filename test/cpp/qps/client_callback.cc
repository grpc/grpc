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
  CallbackClientRpcContext()
      : response_(), context_(), alarm_() {}

  ~CallbackClientRpcContext() {}

  SimpleResponse response_;
  ClientContext context_;
  Alarm alarm_;
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
    ctxs_.resize(num_threads_);
    threads_done_ = 0;
    SetupLoadTest(config, num_threads_);
  }

  virtual ~CallbackClient() {}

  /**
   * Setup context data before running RPC
   */
  virtual bool InitThreadFuncImpl(size_t thread_idx) {
    ctxs_[thread_idx].reset(new CallbackClientRpcContext);
    return true;
  }

  virtual bool ThreadFuncImpl(Thread* t, size_t thread_idx) = 0;

 protected:
  size_t num_threads_;
  // The below mutex and condition variable is used by main benchmark thread to
  // wait on completion of all RPCs before shutdown
  std::mutex shutdown_mu_;
  std::condition_variable shutdown_cv_;
  // Context data pointers are maintained per thread and reallocated before
  // running a RPC
  std::vector<std::unique_ptr<CallbackClientRpcContext>> ctxs_;
  // Number of threads that have finished issuing RPCs
  size_t threads_done_;

  void IssueCallbackRpc(size_t thread_idx, Thread* t) {
    if (!InitThreadFuncImpl(thread_idx)) {
      return;
    }

    if (!closed_loop_) {
      gpr_timespec next_issue_time = NextIssueTime(thread_idx);
      // Start an alarm callback to run the internal callback after
      // next_issue_time
      ctxs_[thread_idx]->alarm_.experimental().Set(
          next_issue_time,
          [this, t, thread_idx](bool ok) {
            ThreadFuncImpl(t, thread_idx);
          });
    } else {
      ThreadFuncImpl(t, thread_idx);
    }
  }

  /**
   * The main thread of the benchmark will be waiting on DestroyMultithreading.
   * Increment the threads_done_ variable to signify that all Callback RPCs
   * started by this particular thread have been completed and it is ok to join
   * the thread. When the last thread increments the counter it should also
   * signal the main thread's conditional variable.
   */
  void NotifyMainThreadOfThreadCompletion(size_t thread_idx) {
    std::lock_guard<std::mutex> l(shutdown_mu_);
    threads_done_++;
    if (threads_done_ == num_threads_) {
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
   * First function invoked by the thread. This initiates one RPC callback
   */
  void ThreadFunc(size_t thread_idx, Thread* t) override {
    IssueCallbackRpc(thread_idx, t);
  }

  /**
   * Wait until Callback RPCs invoked by all threads are done
   */
  void DestroyMultithreading() final {
    std::unique_lock<std::mutex> l(shutdown_mu_);
    while (threads_done_ != num_threads_) {
      shutdown_cv_.wait(l);
    }
    EndThreads();
  }
};

class CallbackUnaryClient final : public CallbackClient {
 public:
  CallbackUnaryClient(const ClientConfig& config) : CallbackClient(config) {
    StartThreads(num_threads_);
  }
  ~CallbackUnaryClient() {}

  bool ThreadFuncImpl(Thread* t, size_t thread_idx) override {
    auto* stub = channels_[thread_idx % channels_.size()].get_stub();
    GPR_TIMER_SCOPE("CallbackUnaryClient::ThreadFunc", 0);
    double start = UsageTimer::Now();
    stub->experimental_async()->UnaryCall(
        &ctxs_[thread_idx]->context_, &request_, &ctxs_[thread_idx]->response_,
        [this, t, thread_idx, start](grpc::Status s) {
          // Update Histogram with data from the callback run
          HistogramEntry entry;
          if (s.ok()) {
            entry.set_value((UsageTimer::Now() - start) * 1e9);
          }
          entry.set_status(s.error_code());
          t->UpdateHistogram(&entry);

          if (ThreadCompleted()) {
            // Notify thread of completion
            NotifyMainThreadOfThreadCompletion(thread_idx);
          } else {
            // Issue a new RPC
            IssueCallbackRpc(thread_idx, t);
          }
        });
    return true;
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
