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
#include <grpc/support/histogram.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

#include "src/core/lib/profiling/timers.h"
#include "src/proto/grpc/testing/services.grpc.pb.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/histogram.h"
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
  void WaitToIssue(int thread_idx) {
    if (!closed_loop_) {
      gpr_sleep_until(NextIssueTime(thread_idx));
    }
  }

  size_t num_threads_;
  std::vector<SimpleResponse> responses_;
};

class SynchronousUnaryClient GRPC_FINAL : public SynchronousClient {
 public:
  SynchronousUnaryClient(const ClientConfig& config)
      : SynchronousClient(config) {
    StartThreads(num_threads_);
  }
  ~SynchronousUnaryClient() { EndThreads(); }

  bool ThreadFunc(Histogram* histogram, size_t thread_idx) GRPC_OVERRIDE {
    WaitToIssue(thread_idx);
    auto* stub = channels_[thread_idx % channels_.size()].get_stub();
    double start = UsageTimer::Now();
    GPR_TIMER_SCOPE("SynchronousUnaryClient::ThreadFunc", 0);
    grpc::ClientContext context;
    grpc::Status s =
        stub->UnaryCall(&context, request_, &responses_[thread_idx]);
    histogram->Add((UsageTimer::Now() - start) * 1e9);
    return s.ok();
  }
};

class SynchronousStreamingClient GRPC_FINAL : public SynchronousClient {
 public:
  SynchronousStreamingClient(const ClientConfig& config)
      : SynchronousClient(config) {
    context_ = new grpc::ClientContext[num_threads_];
    stream_ = new std::unique_ptr<
        grpc::ClientReaderWriter<SimpleRequest, SimpleResponse>>[num_threads_];
    for (size_t thread_idx = 0; thread_idx < num_threads_; thread_idx++) {
      auto* stub = channels_[thread_idx % channels_.size()].get_stub();
      stream_[thread_idx] = stub->StreamingCall(&context_[thread_idx]);
    }
    StartThreads(num_threads_);
  }
  ~SynchronousStreamingClient() {
    EndThreads();
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
    delete[] stream_;
    delete[] context_;
  }

  bool ThreadFunc(Histogram* histogram, size_t thread_idx) GRPC_OVERRIDE {
    WaitToIssue(thread_idx);
    GPR_TIMER_SCOPE("SynchronousStreamingClient::ThreadFunc", 0);
    double start = UsageTimer::Now();
    if (stream_[thread_idx]->Write(request_) &&
        stream_[thread_idx]->Read(&responses_[thread_idx])) {
      histogram->Add((UsageTimer::Now() - start) * 1e9);
      return true;
    }
    return false;
  }

 private:
  // These are both conceptually std::vector but cannot be for old compilers
  // that expect contained classes to support copy constructors
  grpc::ClientContext* context_;
  std::unique_ptr<grpc::ClientReaderWriter<SimpleRequest, SimpleResponse>>*
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
