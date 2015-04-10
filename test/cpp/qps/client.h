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

#ifndef TEST_QPS_CLIENT_H
#define TEST_QPS_CLIENT_H

#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/timer.h"
#include "test/cpp/qps/qpstest.grpc.pb.h"

#include <condition_variable>
#include <mutex>

namespace grpc {
namespace testing {

class Client {
 public:
  explicit Client(const ClientConfig& config) : timer_(new Timer) {
    for (int i = 0; i < config.client_channels(); i++) {
      channels_.push_back(ClientChannelInfo(
          config.server_targets(i % config.server_targets_size()), config));
    }
    request_.set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
    request_.set_response_size(config.payload_size());
  }
  virtual ~Client() {}

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
    latencies.FillProto(stats.mutable_latencies());
    stats.set_time_elapsed(timer_result.wall);
    stats.set_time_system(timer_result.system);
    stats.set_time_user(timer_result.user);
    return stats;
  }

 protected:
  SimpleRequest request_;

  class ClientChannelInfo {
   public:
    ClientChannelInfo(const grpc::string& target, const ClientConfig& config)
        : channel_(CreateTestChannel(target, config.enable_ssl())),
          stub_(TestService::NewStub(channel_)) {}
    ChannelInterface* get_channel() { return channel_.get(); }
    TestService::Stub* get_stub() { return stub_.get(); }

   private:
    std::shared_ptr<ChannelInterface> channel_;
    std::unique_ptr<TestService::Stub> stub_;
  };
  std::vector<ClientChannelInfo> channels_;

  void StartThreads(size_t num_threads) {
    for (size_t i = 0; i < num_threads; i++) {
      threads_.emplace_back(new Thread(this, i));
    }
  }

  void EndThreads() { threads_.clear(); }

  virtual void ThreadFunc(Histogram* histogram, size_t thread_idx) = 0;

 private:
  class Thread {
   public:
    Thread(Client* client, size_t idx)
        : done_(false),
          new_(nullptr),
          impl_([this, idx, client]() {
            for (;;) {
              // run the loop body
	      client->ThreadFunc(&histogram_, idx);
              // lock, see if we're done
              std::lock_guard<std::mutex> g(mu_);
              if (done_) {return;}
	      // check if we're marking, swap out the histogram if so
	      if (new_) {
                new_->Swap(&histogram_);
                new_ = nullptr;
                cv_.notify_one();
              }
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

  std::vector<std::unique_ptr<Thread>> threads_;
  std::unique_ptr<Timer> timer_;
};

std::unique_ptr<Client>
  CreateSynchronousUnaryClient(const ClientConfig& args);
std::unique_ptr<Client>
  CreateSynchronousStreamingClient(const ClientConfig& args);
std::unique_ptr<Client> CreateAsyncUnaryClient(const ClientConfig& args);
std::unique_ptr<Client> CreateAsyncStreamingClient(const ClientConfig& args);

}  // namespace testing
}  // namespace grpc

#endif
