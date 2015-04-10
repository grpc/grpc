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
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sstream>

#include <sys/signal.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/histogram.h>
#include <grpc/support/log.h>
#include <grpc/support/host_port.h>
#include <gflags/gflags.h>
#include <grpc++/client_context.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include <gtest/gtest.h>
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/qpstest.grpc.pb.h"
#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/timer.h"

namespace grpc {
namespace testing {

class SynchronousClient : public Client {
 public:
  SynchronousClient(const ClientConfig& config) : Client(config) {
    num_threads_ =
      config.outstanding_rpcs_per_channel() * config.client_channels();
    responses_.resize(num_threads_);
  }

  virtual ~SynchronousClient() { EndThreads(); }

 protected:
  size_t num_threads_;
  std::vector<SimpleResponse> responses_;
};

class SynchronousUnaryClient GRPC_FINAL : public SynchronousClient {
 public:
  SynchronousUnaryClient(const ClientConfig& config):
    SynchronousClient(config) {StartThreads(num_threads_);}
  ~SynchronousUnaryClient() {}
  
  void ThreadFunc(Histogram* histogram, size_t thread_idx) GRPC_OVERRIDE {
    auto* stub = channels_[thread_idx % channels_.size()].get_stub();
    double start = Timer::Now();
    grpc::ClientContext context;
    grpc::Status s =
        stub->UnaryCall(&context, request_, &responses_[thread_idx]);
    histogram->Add((Timer::Now() - start) * 1e9);
  }
};

class SynchronousStreamingClient GRPC_FINAL : public SynchronousClient {
 public:
  SynchronousStreamingClient(const ClientConfig& config):
    SynchronousClient(config) {
    for (size_t thread_idx=0;thread_idx<num_threads_;thread_idx++){
      auto* stub = channels_[thread_idx % channels_.size()].get_stub();
      stream_ = stub->StreamingCall(&context_);
    }
    StartThreads(num_threads_);
  }
  ~SynchronousStreamingClient() {
    if (stream_) {
      SimpleResponse response;
      stream_->WritesDone();
      EXPECT_TRUE(stream_->Finish().IsOk());
    }
  }

  void ThreadFunc(Histogram* histogram, size_t thread_idx) GRPC_OVERRIDE {
    double start = Timer::Now();
    EXPECT_TRUE(stream_->Write(request_));
    EXPECT_TRUE(stream_->Read(&responses_[thread_idx]));
    histogram->Add((Timer::Now() - start) * 1e9);
  }
  private:
    grpc::ClientContext context_;
    std::unique_ptr<grpc::ClientReaderWriter<SimpleRequest,
                                             SimpleResponse>> stream_;
};

std::unique_ptr<Client>
CreateSynchronousUnaryClient(const ClientConfig& config) {
  return std::unique_ptr<Client>(new SynchronousUnaryClient(config));
}
std::unique_ptr<Client>
CreateSynchronousStreamingClient(const ClientConfig& config) {
  return std::unique_ptr<Client>(new SynchronousStreamingClient(config));
}

}  // namespace testing
}  // namespace grpc
