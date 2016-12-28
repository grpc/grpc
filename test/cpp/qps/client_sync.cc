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

#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

#include "src/core/lib/profiling/timers.h"
#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/interarrival.h"
#include "test/cpp/qps/usage_timer.h"

namespace grpc {
namespace testing {

static std::unique_ptr<BenchmarkService::Stub> BenchmarkStubCreator(
    std::shared_ptr<Channel> ch) {
  return BenchmarkService::NewStub(ch);
}

class SynchronousClient
    : public ClientImpl<BenchmarkService::Stub, SimpleRequest> {
 public:
  SynchronousClient(const ClientConfig& config)
      : ClientImpl<BenchmarkService::Stub, SimpleRequest>(
            config, BenchmarkStubCreator) {
    num_threads_ =
        config.outstanding_rpcs_per_channel() * config.client_channels();
    responses_.resize(num_threads_);
    SetupLoadTest(config, num_threads_);
  }

  virtual ~SynchronousClient(){};

 protected:
  // WaitToIssue returns false if we realize that we need to break out
  bool WaitToIssue(int thread_idx) {
    if (!closed_loop_) {
      const gpr_timespec next_issue_time = NextIssueTime(thread_idx);
      // Avoid sleeping for too long continuously because we might
      // need to terminate before then. This is an issue since
      // exponential distribution can occasionally produce bad outliers
      while (true) {
        const gpr_timespec one_sec_delay =
            gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                         gpr_time_from_seconds(1, GPR_TIMESPAN));
        if (gpr_time_cmp(next_issue_time, one_sec_delay) <= 0) {
          gpr_sleep_until(next_issue_time);
          return true;
        } else {
          gpr_sleep_until(one_sec_delay);
          if (gpr_atm_acq_load(&thread_pool_done_) != static_cast<gpr_atm>(0)) {
            return false;
          }
        }
      }
    }
    return true;
  }

  size_t num_threads_;
  std::vector<SimpleResponse> responses_;

 private:
  void DestroyMultithreading() override final { EndThreads(); }
};

class SynchronousUnaryClient final : public SynchronousClient {
 public:
  SynchronousUnaryClient(const ClientConfig& config)
      : SynchronousClient(config) {
    StartThreads(num_threads_);
  }
  ~SynchronousUnaryClient() {}

  bool ThreadFunc(HistogramEntry* entry, size_t thread_idx) override {
    if (!WaitToIssue(thread_idx)) {
      return true;
    }
    auto* stub = channels_[thread_idx % channels_.size()].get_stub();
    double start = UsageTimer::Now();
    GPR_TIMER_SCOPE("SynchronousUnaryClient::ThreadFunc", 0);
    grpc::ClientContext context;
    grpc::Status s =
        stub->UnaryCall(&context, request_, &responses_[thread_idx]);
    entry->set_value((UsageTimer::Now() - start) * 1e9);
    entry->set_status(s.error_code());
    return true;
  }
};

class SynchronousStreamingClient final : public SynchronousClient {
 public:
  SynchronousStreamingClient(const ClientConfig& config)
      : SynchronousClient(config),
        context_(num_threads_),
        stream_(num_threads_) {
    for (size_t thread_idx = 0; thread_idx < num_threads_; thread_idx++) {
      auto* stub = channels_[thread_idx % channels_.size()].get_stub();
      stream_[thread_idx] = stub->StreamingCall(&context_[thread_idx]);
    }
    StartThreads(num_threads_);
  }
  ~SynchronousStreamingClient() {
    for (size_t i = 0; i < num_threads_; i++) {
      auto stream = &stream_[i];
      if (*stream) {
        (*stream)->WritesDone();
        Status s = (*stream)->Finish();
        EXPECT_TRUE(s.ok());
        if (!s.ok()) {
          gpr_log(GPR_ERROR, "Stream %zu received an error %s", i,
                  s.error_message().c_str());
        }
      }
    }
  }

  bool ThreadFunc(HistogramEntry* entry, size_t thread_idx) override {
    if (!WaitToIssue(thread_idx)) {
      return true;
    }
    GPR_TIMER_SCOPE("SynchronousStreamingClient::ThreadFunc", 0);
    double start = UsageTimer::Now();
    if (stream_[thread_idx]->Write(request_) &&
        stream_[thread_idx]->Read(&responses_[thread_idx])) {
      entry->set_value((UsageTimer::Now() - start) * 1e9);
      return true;
    }
    return false;
  }

 private:
  // These are both conceptually std::vector but cannot be for old compilers
  // that expect contained classes to support copy constructors
  std::vector<grpc::ClientContext> context_;
  std::vector<
      std::unique_ptr<grpc::ClientReaderWriter<SimpleRequest, SimpleResponse>>>
      stream_;
};

std::unique_ptr<Client> CreateSynchronousUnaryClient(
    const ClientConfig& config) {
  return std::unique_ptr<Client>(new SynchronousUnaryClient(config));
}
std::unique_ptr<Client> CreateSynchronousStreamingClient(
    const ClientConfig& config) {
  return std::unique_ptr<Client>(new SynchronousStreamingClient(config));
}

}  // namespace testing
}  // namespace grpc
