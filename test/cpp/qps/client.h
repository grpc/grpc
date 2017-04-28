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
#include <unordered_map>
#include <vector>

#include <grpc++/channel.h>
#include <grpc++/support/byte_buffer.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc++/support/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/proto/grpc/testing/payloads.pb.h"
#include "src/proto/grpc/testing/services.grpc.pb.h"

#include "test/cpp/qps/histogram.h"
#include "test/cpp/qps/interarrival.h"
#include "test/cpp/qps/usage_timer.h"
#include "test/cpp/util/create_test_channel.h"

namespace grpc {
namespace testing {

template <class RequestType>
class ClientRequestCreator {
 public:
  ClientRequestCreator(RequestType* req, const PayloadConfig&) {
    // this template must be specialized
    // fail with an assertion rather than a compile-time
    // check since these only happen at the beginning anyway
    GPR_ASSERT(false);
  }
};

template <>
class ClientRequestCreator<SimpleRequest> {
 public:
  ClientRequestCreator(SimpleRequest* req,
                       const PayloadConfig& payload_config) {
    if (payload_config.has_bytebuf_params()) {
      GPR_ASSERT(false);  // not appropriate for this specialization
    } else if (payload_config.has_simple_params()) {
      req->set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
      req->set_response_size(payload_config.simple_params().resp_size());
      req->mutable_payload()->set_type(
          grpc::testing::PayloadType::COMPRESSABLE);
      int size = payload_config.simple_params().req_size();
      std::unique_ptr<char[]> body(new char[size]);
      req->mutable_payload()->set_body(body.get(), size);
    } else if (payload_config.has_complex_params()) {
      GPR_ASSERT(false);  // not appropriate for this specialization
    } else {
      // default should be simple proto without payloads
      req->set_response_type(grpc::testing::PayloadType::COMPRESSABLE);
      req->set_response_size(0);
      req->mutable_payload()->set_type(
          grpc::testing::PayloadType::COMPRESSABLE);
    }
  }
};

template <>
class ClientRequestCreator<ByteBuffer> {
 public:
  ClientRequestCreator(ByteBuffer* req, const PayloadConfig& payload_config) {
    if (payload_config.has_bytebuf_params()) {
      std::unique_ptr<char[]> buf(
          new char[payload_config.bytebuf_params().req_size()]);
      grpc_slice s = grpc_slice_from_copied_buffer(
          buf.get(), payload_config.bytebuf_params().req_size());
      Slice slice(s, Slice::STEAL_REF);
      *req = ByteBuffer(&slice, 1);
    } else {
      GPR_ASSERT(false);  // not appropriate for this specialization
    }
  }
};

class HistogramEntry final {
 public:
  HistogramEntry() : value_used_(false), status_used_(false) {}
  bool value_used() const { return value_used_; }
  double value() const { return value_; }
  void set_value(double v) {
    value_used_ = true;
    value_ = v;
  }
  bool status_used() const { return status_used_; }
  int status() const { return status_; }
  void set_status(int status) {
    status_used_ = true;
    status_ = status;
  }

 private:
  bool value_used_;
  double value_;
  bool status_used_;
  int status_;
};

typedef std::unordered_map<int, int64_t> StatusHistogram;

inline void MergeStatusHistogram(const StatusHistogram& from,
                                 StatusHistogram* to) {
  for (StatusHistogram::const_iterator it = from.begin(); it != from.end();
       ++it) {
    (*to)[it->first] += it->second;
  }
}

class Client {
 public:
  Client()
      : timer_(new UsageTimer),
        interarrival_timer_(),
        started_requests_(false) {
    gpr_event_init(&start_requests_);
  }
  virtual ~Client() {}

  ClientStats Mark(bool reset) {
    Histogram latencies;
    StatusHistogram statuses;
    UsageTimer::Result timer_result;

    MaybeStartRequests();

    if (reset) {
      std::vector<Histogram> to_merge(threads_.size());
      std::vector<StatusHistogram> to_merge_status(threads_.size());

      for (size_t i = 0; i < threads_.size(); i++) {
        threads_[i]->BeginSwap(&to_merge[i], &to_merge_status[i]);
      }
      std::unique_ptr<UsageTimer> timer(new UsageTimer);
      timer_.swap(timer);
      for (size_t i = 0; i < threads_.size(); i++) {
        latencies.Merge(to_merge[i]);
        MergeStatusHistogram(to_merge_status[i], &statuses);
      }
      timer_result = timer->Mark();
    } else {
      // merge snapshots of each thread histogram
      for (size_t i = 0; i < threads_.size(); i++) {
        threads_[i]->MergeStatsInto(&latencies, &statuses);
      }
      timer_result = timer_->Mark();
    }

    ClientStats stats;
    latencies.FillProto(stats.mutable_latencies());
    for (StatusHistogram::const_iterator it = statuses.begin();
         it != statuses.end(); ++it) {
      RequestResultCount* rrc = stats.add_request_results();
      rrc->set_status_code(it->first);
      rrc->set_count(it->second);
    }
    stats.set_time_elapsed(timer_result.wall);
    stats.set_time_system(timer_result.system);
    stats.set_time_user(timer_result.user);
    return stats;
  }

  // Must call AwaitThreadsCompletion before destructor to avoid a race
  // between destructor and invocation of virtual ThreadFunc
  void AwaitThreadsCompletion() {
    gpr_atm_rel_store(&thread_pool_done_, static_cast<gpr_atm>(true));
    DestroyMultithreading();
    std::unique_lock<std::mutex> g(thread_completion_mu_);
    while (threads_remaining_ != 0) {
      threads_complete_.wait(g);
    }
  }

 protected:
  bool closed_loop_;
  gpr_atm thread_pool_done_;

  void StartThreads(size_t num_threads) {
    gpr_atm_rel_store(&thread_pool_done_, static_cast<gpr_atm>(false));
    threads_remaining_ = num_threads;
    for (size_t i = 0; i < num_threads; i++) {
      threads_.emplace_back(new Thread(this, i));
    }
  }

  void EndThreads() {
    MaybeStartRequests();
    threads_.clear();
  }

  virtual void DestroyMultithreading() = 0;
  virtual bool ThreadFunc(HistogramEntry* histogram, size_t thread_idx) = 0;

  void SetupLoadTest(const ClientConfig& config, size_t num_threads) {
    // Set up the load distribution based on the number of threads
    const auto& load = config.load_params();

    std::unique_ptr<RandomDistInterface> random_dist;
    switch (load.load_case()) {
      case LoadParams::kClosedLoop:
        // Closed-loop doesn't use random dist at all
        break;
      case LoadParams::kPoisson:
        random_dist.reset(
            new ExpDist(load.poisson().offered_load() / num_threads));
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
      const auto now = gpr_now(GPR_CLOCK_MONOTONIC);
      for (size_t i = 0; i < num_threads; i++) {
        next_time_.push_back(gpr_time_add(
            now,
            gpr_time_from_nanos(interarrival_timer_.next(i), GPR_TIMESPAN)));
      }
    }
  }

  gpr_timespec NextIssueTime(int thread_idx) {
    const gpr_timespec result = next_time_[thread_idx];
    next_time_[thread_idx] =
        gpr_time_add(next_time_[thread_idx],
                     gpr_time_from_nanos(interarrival_timer_.next(thread_idx),
                                         GPR_TIMESPAN));
    return result;
  }
  std::function<gpr_timespec()> NextIssuer(int thread_idx) {
    return closed_loop_ ? std::function<gpr_timespec()>()
                        : std::bind(&Client::NextIssueTime, this, thread_idx);
  }

 private:
  class Thread {
   public:
    Thread(Client* client, size_t idx)
        : client_(client), idx_(idx), impl_(&Thread::ThreadFunc, this) {}

    ~Thread() { impl_.join(); }

    void BeginSwap(Histogram* n, StatusHistogram* s) {
      std::lock_guard<std::mutex> g(mu_);
      n->Swap(&histogram_);
      s->swap(statuses_);
    }

    void MergeStatsInto(Histogram* hist, StatusHistogram* s) {
      std::unique_lock<std::mutex> g(mu_);
      hist->Merge(histogram_);
      MergeStatusHistogram(statuses_, s);
    }

   private:
    Thread(const Thread&);
    Thread& operator=(const Thread&);

    void ThreadFunc() {
      while (!gpr_event_wait(
          &client_->start_requests_,
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_seconds(1, GPR_TIMESPAN)))) {
        gpr_log(GPR_INFO, "Waiting for benchmark to start");
      }

      for (;;) {
        // run the loop body
        HistogramEntry entry;
        const bool thread_still_ok = client_->ThreadFunc(&entry, idx_);
        // lock, update histogram if needed and see if we're done
        std::lock_guard<std::mutex> g(mu_);
        if (entry.value_used()) {
          histogram_.Add(entry.value());
        }
        if (entry.status_used()) {
          statuses_[entry.status()]++;
        }
        if (!thread_still_ok) {
          gpr_log(GPR_ERROR, "Finishing client thread due to RPC error");
        }
        if (!thread_still_ok ||
            static_cast<bool>(gpr_atm_acq_load(&client_->thread_pool_done_))) {
          client_->CompleteThread();
          return;
        }
      }
    }

    std::mutex mu_;
    Histogram histogram_;
    StatusHistogram statuses_;
    Client* client_;
    const size_t idx_;
    std::thread impl_;
  };

  std::vector<std::unique_ptr<Thread>> threads_;
  std::unique_ptr<UsageTimer> timer_;

  InterarrivalTimer interarrival_timer_;
  std::vector<gpr_timespec> next_time_;

  std::mutex thread_completion_mu_;
  size_t threads_remaining_;
  std::condition_variable threads_complete_;

  gpr_event start_requests_;
  bool started_requests_;

  void MaybeStartRequests() {
    if (!started_requests_) {
      started_requests_ = true;
      gpr_event_set(&start_requests_, (void*)1);
    }
  }

  void CompleteThread() {
    std::lock_guard<std::mutex> g(thread_completion_mu_);
    threads_remaining_--;
    if (threads_remaining_ == 0) {
      threads_complete_.notify_all();
    }
  }
};

template <class StubType, class RequestType>
class ClientImpl : public Client {
 public:
  ClientImpl(const ClientConfig& config,
             std::function<std::unique_ptr<StubType>(std::shared_ptr<Channel>)>
                 create_stub)
      : cores_(gpr_cpu_num_cores()),
        channels_(config.client_channels()),
        create_stub_(create_stub) {
    for (int i = 0; i < config.client_channels(); i++) {
      channels_[i].init(config.server_targets(i % config.server_targets_size()),
                        config, create_stub_, i);
    }

    ClientRequestCreator<RequestType> create_req(&request_,
                                                 config.payload_config());
  }
  virtual ~ClientImpl() {}

 protected:
  const int cores_;
  RequestType request_;

  class ClientChannelInfo {
   public:
    ClientChannelInfo() {}
    ClientChannelInfo(const ClientChannelInfo& i) {
      // The copy constructor is to satisfy old compilers
      // that need it for using std::vector . It is only ever
      // used for empty entries
      GPR_ASSERT(!i.channel_ && !i.stub_);
    }
    void init(const grpc::string& target, const ClientConfig& config,
              std::function<std::unique_ptr<StubType>(std::shared_ptr<Channel>)>
                  create_stub,
              int shard) {
      // We have to use a 2-phase init like this with a default
      // constructor followed by an initializer function to make
      // old compilers happy with using this in std::vector
      ChannelArguments args;
      args.SetInt("shard_to_ensure_no_subchannel_merges", shard);
      set_channel_args(config, &args);
      channel_ = CreateTestChannel(
          target, config.security_params().server_host_override(),
          config.has_security_params(), !config.security_params().use_test_ca(),
          std::shared_ptr<CallCredentials>(), args);
      gpr_log(GPR_INFO, "Connecting to %s", target.c_str());
      GPR_ASSERT(channel_->WaitForConnected(
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                       gpr_time_from_seconds(300, GPR_TIMESPAN))));
      stub_ = create_stub(channel_);
    }
    Channel* get_channel() { return channel_.get(); }
    StubType* get_stub() { return stub_.get(); }

   private:
    void set_channel_args(const ClientConfig& config, ChannelArguments* args) {
      for (auto channel_arg : config.channel_args()) {
        if (channel_arg.value_case() == ChannelArg::kStrValue) {
          args->SetString(channel_arg.name(), channel_arg.str_value());
        } else if (channel_arg.value_case() == ChannelArg::kIntValue) {
          args->SetInt(channel_arg.name(), channel_arg.int_value());
        } else {
          gpr_log(GPR_ERROR, "Empty channel arg value.");
        }
      }
    }

    std::shared_ptr<Channel> channel_;
    std::unique_ptr<StubType> stub_;
  };
  std::vector<ClientChannelInfo> channels_;
  std::function<std::unique_ptr<StubType>(const std::shared_ptr<Channel>&)>
      create_stub_;
};

std::unique_ptr<Client> CreateSynchronousUnaryClient(const ClientConfig& args);
std::unique_ptr<Client> CreateSynchronousStreamingClient(
    const ClientConfig& args);
std::unique_ptr<Client> CreateAsyncUnaryClient(const ClientConfig& args);
std::unique_ptr<Client> CreateAsyncStreamingClient(const ClientConfig& args);
std::unique_ptr<Client> CreateGenericAsyncStreamingClient(
    const ClientConfig& args);

}  // namespace testing
}  // namespace grpc

#endif
