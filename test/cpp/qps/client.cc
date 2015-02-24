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
#include "test/cpp/qps/qpstest.pb.h"

DEFINE_int32(driver_port, 0, "Client driver port.");

using grpc::ChannelInterface;
using grpc::CreateTestChannel;
using grpc::ServerBuilder;
using grpc::testing::ClientArgs;
using grpc::testing::ClientResult;
using grpc::testing::QpsClient;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StatsRequest;
using grpc::testing::TestService;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google { }
namespace gflags { }
using namespace google;
using namespace gflags;

static double now() {
  gpr_timespec tv = gpr_now();
  return 1e9 * tv.tv_sec + tv.tv_nsec;
}

static bool got_sigint = false;

static void sigint_handler(int x) { got_sigint = 1; }

ClientResult RunTest(const ClientArgs& args) {
  gpr_log(GPR_INFO,
          "QPS test with parameters\n"
          "enable_ssl = %d\n"
          "client_channels = %d\n"
          "client_threads = %d\n"
          "num_rpcs = %d\n"
          "payload_size = %d\n"
          "server_target = %s\n\n",
          args.enable_ssl(), args.client_channels(), args.client_threads(), args.num_rpcs(),
          args.payload_size(), args.server_target().c_str());

  class ClientChannelInfo {
   public:
    explicit ClientChannelInfo(const ClientArgs& args)
        : channel_(CreateTestChannel(args.server_target(), args.enable_ssl())),
          stub_(TestService::NewStub(channel_)) {}
    ChannelInterface *get_channel() { return channel_.get(); }
    TestService::Stub *get_stub() { return stub_.get(); }

   private:
    std::shared_ptr<ChannelInterface> channel_;
    std::unique_ptr<TestService::Stub> stub_;
  };

  std::vector<ClientChannelInfo> channels;
  for (int i = 0; i < args.client_channels(); i++) {
    channels.push_back(ClientChannelInfo(args));
  }

  std::vector<std::thread> threads;  // Will add threads when ready to execute
  std::vector< ::gpr_histogram *> thread_stats(args.client_threads());

  grpc::ClientContext context_stats_begin;

  grpc_profiler_start("qps_client.prof");

  gpr_timespec start = gpr_now();

  for (int i = 0; i < args.client_threads(); i++) {
    gpr_histogram *hist = gpr_histogram_create(0.01, 60e9);
    GPR_ASSERT(hist != NULL);
    thread_stats[i] = hist;

    threads.push_back(
        std::thread([hist, args, &channels](int channel_num) {
                      SimpleRequest request;
                      SimpleResponse response;
                      request.set_response_type(
                          grpc::testing::PayloadType::COMPRESSABLE);
                      request.set_response_size(args.payload_size());

                      for (int j = 0; j < args.num_rpcs(); j++) {
                        TestService::Stub *stub =
                            channels[channel_num].get_stub();
                        double start = now();
                        grpc::ClientContext context;
                        grpc::Status s =
                            stub->UnaryCall(&context, request, &response);
                        gpr_histogram_add(hist, now() - start);

                        GPR_ASSERT((s.code() == grpc::StatusCode::OK) &&
                                   (response.payload().type() ==
                                    grpc::testing::PayloadType::COMPRESSABLE) &&
                                   (response.payload().body().length() ==
                                    static_cast<size_t>(args.payload_size())));

                        // Now do runtime round-robin assignment of the next
                        // channel number
                        channel_num += args.client_threads();
                        channel_num %= args.client_channels();
                      }
                    },
                    i % args.client_channels()));
  }

  for (auto &t : threads) {
    t.join();
  }

  gpr_timespec stop = gpr_now();

  grpc_profiler_stop();

  gpr_histogram *hist = gpr_histogram_create(0.01, 60e9);
  GPR_ASSERT(hist != NULL);

  for (int i = 0; i < args.client_threads(); i++) {
    gpr_histogram *h = thread_stats[i];
    gpr_log(GPR_INFO, "latency at thread %d (50/90/95/99/99.9): %f/%f/%f/%f/%f",
            i, gpr_histogram_percentile(h, 50), gpr_histogram_percentile(h, 90),
            gpr_histogram_percentile(h, 95), gpr_histogram_percentile(h, 99),
            gpr_histogram_percentile(h, 99.9));
    gpr_histogram_merge(hist, h);
    gpr_histogram_destroy(h);
  }

  ClientResult result;
  auto* latencies = result.mutable_latencies();
  latencies->set_l_50(gpr_histogram_percentile(hist, 50));
  latencies->set_l_90(gpr_histogram_percentile(hist, 90));
  latencies->set_l_99(gpr_histogram_percentile(hist, 99));
  latencies->set_l_999(gpr_histogram_percentile(hist, 99.9));
  gpr_timespec elapsed = gpr_time_sub(stop, start);
  result.set_num_rpcs(args.client_threads() * args.num_rpcs());
  result.set_time_elapsed(elapsed.tv_sec + 1e-9 * elapsed.tv_nsec);

  gpr_histogram_destroy(hist);

  return result;
}

class ClientImpl : public QpsClient::Service {
 public:

 private:
  std::mutex client_mu_;
};

static void RunServer() {
  char* server_address = NULL;
  gpr_join_host_port(&server_address, "::", FLAGS_driver_port);

  ClientImpl service;

  ServerBuilder builder;
  builder.AddPort(server_address);
  builder.RegisterService(&service);

  gpr_free(server_address);

  auto server = builder.BuildAndStart();

  while (!got_sigint) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

int main(int argc, char **argv) {
  grpc_init();
  ParseCommandLineFlags(&argc, &argv, true);

  signal(SIGINT, sigint_handler);

  RunServer();

  grpc_shutdown();
  return 0;
}
