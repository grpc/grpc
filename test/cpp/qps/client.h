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

#include <condition_variable>
#include <mutex>

#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/interarrival.h"
#include "test/cpp/qps/timer.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/proto/benchmarks/payloads.grpc.pb.h"
#include "test/proto/benchmarks/services.grpc.pb.h"

namespace grpc {

#if defined(__APPLE__)
// Specialize Timepoint for high res clock as we need that
template <>
class TimePoint<std::chrono::high_resolution_clock::time_point> {
 public:
  TimePoint(const std::chrono::high_resolution_clock::time_point& time) {
    TimepointHR2Timespec(time, &time_);
  }
  gpr_timespec raw_time() const { return time_; }

 private:
  gpr_timespec time_;
};
#endif

namespace testing {

typedef std::chrono::high_resolution_clock grpc_time_source;
typedef std::chrono::time_point<grpc_time_source> grpc_time;

class Client {
 public:
  explicit Client(const ClientConfig& config)
      : channels_(config.client_channels()),
        timer_(new Timer),
        interarrival_timer_() {
    for (int i = 0; i < config.client_channels(); i++) {
      channels_[i].init(config.server_targets(i % config.server_targets_size()),
                        config);
    }
    if (config.payload_config().has_bytebuf_params()) {
      GPR_ASSERT(false);  // not yet implemented
    } else if (config.payload_config().has_simple_params()) {
      request_.set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
      request_.set_response_size(
          config.payload_config().simple_params().resp_size());
      request_.mutable_payload()->set_type(
          grpc::testing::PayloadType::COMPRESSABLE);
      int size = config.payload_config().simple_params().req_size();
      std::unique_ptr<char[]> body(new char[size]);
      request_.mutable_payload()->set_body(body.get(), size);
    } else if (config.payload_config().has_complex_params()) {
      GPR_ASSERT(false);  // not yet implemented
    } else {
      // default should be simple proto without payloads
      request_.set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
      request_.set_response_size(0);
      request_.mutable_payload()->set_type(
          grpc::testing::PayloadType::COMPRESSABLE);
    }
  }
  virtual ~Client() {}

  ClientStats Mark(bool reset) {
    Histogram latencies;
    Timer::Result timer_result;

    // avoid std::vector for old compilers that expect a copy constructor
    if (reset) {
      Histogram* to_merge = new Histogram[threads_.size()];
      for (size_t i = 0; i < threads_.size(); i++) {
        threads_[i]->BeginSwap(&to_merge[i]);
      }
      std::unique_ptr<Timer> timer(new Timer);
      timer_.swap(timer);
      for (size_t i = 0; i < threads_.size(); i++) {
        threads_[i]->EndSwap();
        latencies.Merge(to_merge[i]);
      }
      delete[] to_merge;
      timer_result = timer->Mark();
    } else {
      // merge snapshots of each thread histogram
      for (size_t i = 0; i < threads_.size(); i++) {
        threads_[i]->MergeStatsInto(&latencies);
      }
      timer_result = timer_->Mark();
    }

    ClientStats stats;
    latencies.FillProto(stats.mutable_latencies());
    stats.set_time_elapsed(timer_result.wall);
    stats.set_time_system(timer_result.system);
    stats.set_time_user(timer_result.user);
    return stats;
  }

 protected:
  SimpleRequest request_;
  bool closed_loop_;

  class ClientChannelInfo {
   public:
    ClientChannelInfo() {}
    ClientChannelInfo(const ClientChannelInfo& i) {
      // The copy constructor is to satisfy old compilers
      // that need it for using std::vector . It is only ever
      // used for empty entries
      GPR_ASSERT(!i.channel_ && !i.stub_);
    }
    void init(const grpc::string& target, const ClientConfig& config) {
      // We have to use a 2-phase init like this with a default
      // constructor followed by an initializer function to make
      // old compilers happy with using this in std::vector
      channel_ = CreateTestChannel(
          target, config.security_params().server_host_override(),
          config.has_security_params(),
          !config.security_params().use_test_ca());
      stub_ = BenchmarkService::NewStub(channel_);
    }
    Channel* get_channel() { return channel_.get(); }
    BenchmarkService::Stub* get_stub() { return stub_.get(); }

   private:
    std::shared_ptr<Channel> channel_;
    std::unique_ptr<BenchmarkService::Stub> stub_;
  };
  std::vector<ClientChannelInfo> channels_;

  void StartThreads(size_t num_threads) {
    for (size_t i = 0; i < num_threads; i++) {
      threads_.emplace_back(new Thread(this, i));
    }
  }

  void EndThreads() { threads_.clear(); }

  virtual bool ThreadFunc(Histogram* histogram, size_t thread_idx) = 0;

  void SetupLoadTest(const ClientConfig& config, size_t num_threads) {
    // Set up the load distribution based on the number of threads
    const auto& load = config.load_params();

    std::unique_ptr<RandomDist> random_dist;
    switch (load.load_case()) {
      case LoadParams::kClosedLoop:
        // Closed-loop doesn't use random dist at all
        break;
      case LoadParams::kPoisson:
        random_dist.reset(
            new ExpDist(load.poisson().offered_load() / num_threads));
        break;
      case LoadParams::kUniform:
        random_dist.reset(
            new UniformDist(load.uniform().interarrival_lo() * num_threads,
                            load.uniform().interarrival_hi() * num_threads));
        break;
      case LoadParams::kDeterm:
        random_dist.reset(
            new DetDist(num_threads / load.determ().offered_load()));
        break;
      case LoadParams::kPareto:
        random_dist.reset(
            new ParetoDist(load.pareto().interarrival_base() * num_threads,
                           load.pareto().alpha()));
        break;
      default:
        GPR_ASSERT(false);
    }

    // Set closed_loop_ based on whether or not random_dist is set
    if (!random_dist) {
      closed_loop_ = true;
    } else {
      closed_loop_ = false;
      // set up interarrival timer according to random dist
      interarrival_timer_.init(*random_dist, num_threads);
      for (size_t i = 0; i < num_threads; i++) {
        next_time_.push_back(
            grpc_time_source::now() +
            std::chrono::duration_cast<grpc_time_source::duration>(
                interarrival_timer_(i)));
      }
    }
  }

  bool NextIssueTime(int thread_idx, grpc_time* time_delay) {
    if (closed_loop_) {
      return false;
    } else {
      *time_delay = next_time_[thread_idx];
      next_time_[thread_idx] +=
          std::chrono::duration_cast<grpc_time_source::duration>(
              interarrival_timer_(thread_idx));
      return true;
    }
  }

 private:
  class Thread {
   public:
    Thread(Client* client, size_t idx)
        : done_(false),
          new_stats_(nullptr),
          client_(client),
          idx_(idx),
          impl_(&Thread::ThreadFunc, this) {}

    ~Thread() {
      {
        std::lock_guard<std::mutex> g(mu_);
        done_ = true;
      }
      impl_.join();
    }

    void BeginSwap(Histogram* n) {
      std::lock_guard<std::mutex> g(mu_);
      new_stats_ = n;
    }

    void EndSwap() {
      std::unique_lock<std::mutex> g(mu_);
      while (new_stats_ != nullptr) {
        cv_.wait(g);
      };
    }

    void MergeStatsInto(Histogram* hist) {
      std::unique_lock<std::mutex> g(mu_);
      hist->Merge(histogram_);
    }

   private:
    Thread(const Thread&);
    Thread& operator=(const Thread&);

    void ThreadFunc() {
      for (;;) {
        // run the loop body
        const bool thread_still_ok = client_->ThreadFunc(&histogram_, idx_);
        // lock, see if we're done
        std::lock_guard<std::mutex> g(mu_);
        if (!thread_still_ok) {
          gpr_log(GPR_ERROR, "Finishing client thread due to RPC error");
          done_ = true;
        }
        if (done_) {
          return;
        }
        // check if we're resetting stats, swap out the histogram if so
        if (new_stats_) {
          new_stats_->Swap(&histogram_);
          new_stats_ = nullptr;
          cv_.notify_one();
        }
      }
    }

    BenchmarkService::Stub* stub_;
    ClientConfig config_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_;
    Histogram* new_stats_;
    Histogram histogram_;
    Client* client_;
    size_t idx_;
    std::thread impl_;
  };

  std::vector<std::unique_ptr<Thread>> threads_;
  std::unique_ptr<Timer> timer_;

  InterarrivalTimer interarrival_timer_;
  std::vector<grpc_time> next_time_;
};

std::unique_ptr<Client> CreateSynchronousUnaryClient(const ClientConfig& args);
std::unique_ptr<Client> CreateSynchronousStreamingClient(
    const ClientConfig& args);
std::unique_ptr<Client> CreateAsyncUnaryClient(const ClientConfig& args);
std::unique_ptr<Client> CreateAsyncStreamingClient(const ClientConfig& args);

}  // namespace testing
}  // namespace grpc

#endif
