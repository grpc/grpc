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
#include <grpc++/status.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include "test/core/util/grpc_profiler.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/qpstest.pb.h"
#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/timer.h"

namespace grpc {
namespace testing {

class SynchronousClient GRPC_FINAL : public Client {
 public:
  SynchronousClient(const ClientConfig& config) : timer_(new Timer) {
    for (int i = 0; i < config.client_channels(); i++) {
      channels_.push_back(ClientChannelInfo(
          config.server_targets(i % config.server_targets_size()), config));
      auto* stub = channels_.back().get_stub();
      for (int j = 0; j < config.outstanding_rpcs_per_channel(); j++) {
        threads_.emplace_back(new Thread(stub, config));
      }
    }
  }

  ClientStats Mark() {
    Histogram latencies;
    std::vector<Histogram> to_merge(threads_.size());
    for (size_t i = 0; i < threads_.size(); i++) {
      threads_[i]->BeginSwap(&to_merge[i]);
    }
    std::unique_ptr<Timer> timer(new Timer);
    timer_.swap(timer);
    for (size_t i = 0; i < threads_.size(); i++) {
      threads_[i]->EndSwap();
      latencies.Merge(&to_merge[i]);
    }

    auto timer_result = timer->Mark();

    ClientStats stats;
    auto* l = stats.mutable_latencies();
    l->set_l_50(latencies.Percentile(50));
    l->set_l_90(latencies.Percentile(90));
    l->set_l_99(latencies.Percentile(99));
    l->set_l_999(latencies.Percentile(99.9));
    stats.set_num_rpcs(latencies.Count());
    stats.set_time_elapsed(timer_result.wall);
    stats.set_time_system(timer_result.system);
    stats.set_time_user(timer_result.user);
    return stats;
  }

 private:
  class Thread {
   public:
    Thread(TestService::Stub* stub, const ClientConfig& config)
        : stub_(stub),
          config_(config),
          done_(false),
          new_(nullptr),
          impl_([this]() {
            SimpleRequest request;
            SimpleResponse response;
            request.set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
            request.set_response_size(config_.payload_size());
            for (;;) {
              {
                std::lock_guard<std::mutex> g(mu_);
                if (done_) return;
                if (new_) {
                  new_->Swap(&histogram_);
                  new_ = nullptr;
                  cv_.notify_one();
                }
              }
              double start = Timer::Now();
              grpc::ClientContext context;
              grpc::Status s = stub_->UnaryCall(&context, request, &response);
              histogram_.Add((Timer::Now() - start) * 1e9);
            }
          }) {}

    ~Thread() {
      {
        std::lock_guard<std::mutex> g(mu_);
        done_ = true;
      }
      impl_.join();
    }

    void BeginSwap(Histogram* n) {
      std::lock_guard<std::mutex> g(mu_);
      new_ = n;
    }

    void EndSwap() {
      std::unique_lock<std::mutex> g(mu_);
      cv_.wait(g, [this]() { return new_ == nullptr; });
    }

   private:
    Thread(const Thread&);
    Thread& operator=(const Thread&);

    TestService::Stub* stub_;
    ClientConfig config_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_;
    Histogram* new_;
    Histogram histogram_;
    std::thread impl_;
  };

  class ClientChannelInfo {
   public:
    explicit ClientChannelInfo(const grpc::string& target,
                               const ClientConfig& config)
        : channel_(CreateTestChannel(target, config.enable_ssl())),
          stub_(TestService::NewStub(channel_)) {}
    ChannelInterface* get_channel() { return channel_.get(); }
    TestService::Stub* get_stub() { return stub_.get(); }

   private:
    std::shared_ptr<ChannelInterface> channel_;
    std::unique_ptr<TestService::Stub> stub_;
  };
  std::vector<ClientChannelInfo> channels_;
  std::vector<std::unique_ptr<Thread>> threads_;
  std::unique_ptr<Timer> timer_;
};

std::unique_ptr<Client> CreateSynchronousClient(const ClientConfig& config) {
  return std::unique_ptr<Client>(new SynchronousClient(config));
}

}  // namespace testing
}  // namespace grpc
