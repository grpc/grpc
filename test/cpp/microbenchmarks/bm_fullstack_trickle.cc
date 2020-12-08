/*
 *
 * Copyright 2016 gRPC authors.
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

/* Benchmark gRPC end2end in various configurations */

#include <benchmark/benchmark.h>

#include <fstream>

#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/profiling/timers.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/core/util/trickle_endpoint.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(bool, log, false, "Log state to CSV files");
ABSL_FLAG(int32_t, warmup_megabytes, 1,
          "Number of megabytes to pump before collecting flow control stats");
ABSL_FLAG(int32_t, warmup_iterations, 100,
          "Number of iterations to run before collecting flow control stats");
ABSL_FLAG(int32_t, warmup_max_time_seconds, 10,
          "Maximum number of seconds to run warmup loop");

namespace grpc {
namespace testing {

gpr_atm g_now_us = 0;

static gpr_timespec fake_now(gpr_clock_type clock_type) {
  gpr_timespec t;
  gpr_atm now = gpr_atm_no_barrier_load(&g_now_us);
  t.tv_sec = now / GPR_US_PER_SEC;
  t.tv_nsec = (now % GPR_US_PER_SEC) * GPR_NS_PER_US;
  t.clock_type = clock_type;
  return t;
}

static void inc_time() {
  gpr_atm_no_barrier_fetch_add(&g_now_us, 100);
  grpc_timer_manager_tick();
}

static void* tag(intptr_t x) { return reinterpret_cast<void*>(x); }

template <class A0>
static void write_csv(std::ostream* out, A0&& a0) {
  if (!out) return;
  (*out) << a0 << "\n";
}

template <class A0, class... Arg>
static void write_csv(std::ostream* out, A0&& a0, Arg&&... arg) {
  if (!out) return;
  (*out) << a0 << ",";
  write_csv(out, std::forward<Arg>(arg)...);
}

class TrickledCHTTP2 : public EndpointPairFixture {
 public:
  TrickledCHTTP2(Service* service, bool streaming, size_t req_size,
                 size_t resp_size, size_t kilobits_per_second,
                 grpc_passthru_endpoint_stats* stats)
      : EndpointPairFixture(service, MakeEndpoints(kilobits_per_second, stats),
                            FixtureConfiguration()),
        stats_(stats) {
    if (absl::GetFlag(FLAGS_log)) {
      std::ostringstream fn;
      fn << "trickle." << (streaming ? "streaming" : "unary") << "." << req_size
         << "." << resp_size << "." << kilobits_per_second << ".csv";
      log_ = absl::make_unique<std::ofstream>(fn.str().c_str());
      write_csv(log_.get(), "t", "iteration", "client_backlog",
                "server_backlog", "client_t_stall", "client_s_stall",
                "server_t_stall", "server_s_stall", "client_t_remote",
                "server_t_remote", "client_t_announced", "server_t_announced",
                "client_s_remote_delta", "server_s_remote_delta",
                "client_s_local_delta", "server_s_local_delta",
                "client_s_announced_delta", "server_s_announced_delta",
                "client_peer_iws", "client_local_iws", "client_sent_iws",
                "client_acked_iws", "server_peer_iws", "server_local_iws",
                "server_sent_iws", "server_acked_iws", "client_queued_bytes",
                "server_queued_bytes");
    }
  }

  ~TrickledCHTTP2() override {
    if (stats_ != nullptr) {
      grpc_passthru_endpoint_stats_destroy(stats_);
    }
  }

  void AddToLabel(std::ostream& out, benchmark::State& state) override {
    out << " writes/iter:"
        << (static_cast<double>(stats_->num_writes) /
            static_cast<double>(state.iterations()))
        << " cli_transport_stalls/iter:"
        << (static_cast<double>(
                client_stats_.streams_stalled_due_to_transport_flow_control) /
            static_cast<double>(state.iterations()))
        << " cli_stream_stalls/iter:"
        << (static_cast<double>(
                client_stats_.streams_stalled_due_to_stream_flow_control) /
            static_cast<double>(state.iterations()))
        << " svr_transport_stalls/iter:"
        << (static_cast<double>(
                server_stats_.streams_stalled_due_to_transport_flow_control) /
            static_cast<double>(state.iterations()))
        << " svr_stream_stalls/iter:"
        << (static_cast<double>(
                server_stats_.streams_stalled_due_to_stream_flow_control) /
            static_cast<double>(state.iterations()));
  }

  void Log(int64_t iteration) GPR_ATTRIBUTE_NO_TSAN {
    auto now = gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), start_);
    grpc_chttp2_transport* client =
        reinterpret_cast<grpc_chttp2_transport*>(client_transport_);
    grpc_chttp2_transport* server =
        reinterpret_cast<grpc_chttp2_transport*>(server_transport_);
    grpc_chttp2_stream* client_stream =
        client->stream_map.count == 1
            ? static_cast<grpc_chttp2_stream*>(client->stream_map.values[0])
            : nullptr;
    grpc_chttp2_stream* server_stream =
        server->stream_map.count == 1
            ? static_cast<grpc_chttp2_stream*>(server->stream_map.values[0])
            : nullptr;
    write_csv(
        log_.get(),
        static_cast<double>(now.tv_sec) +
            1e-9 * static_cast<double>(now.tv_nsec),
        iteration, grpc_trickle_get_backlog(endpoint_pair_.client),
        grpc_trickle_get_backlog(endpoint_pair_.server),
        client->lists[GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT].head != nullptr,
        client->lists[GRPC_CHTTP2_LIST_STALLED_BY_STREAM].head != nullptr,
        server->lists[GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT].head != nullptr,
        server->lists[GRPC_CHTTP2_LIST_STALLED_BY_STREAM].head != nullptr,
        client->flow_control->remote_window_,
        server->flow_control->remote_window_,
        client->flow_control->announced_window_,
        server->flow_control->announced_window_,
        client_stream ? client_stream->flow_control->remote_window_delta_ : -1,
        server_stream ? server_stream->flow_control->remote_window_delta_ : -1,
        client_stream ? client_stream->flow_control->local_window_delta_ : -1,
        server_stream ? server_stream->flow_control->local_window_delta_ : -1,
        client_stream ? client_stream->flow_control->announced_window_delta_
                      : -1,
        server_stream ? server_stream->flow_control->announced_window_delta_
                      : -1,
        client->settings[GRPC_PEER_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        client->settings[GRPC_LOCAL_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        client->settings[GRPC_SENT_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        client->settings[GRPC_ACKED_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        server->settings[GRPC_PEER_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        server->settings[GRPC_LOCAL_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        server->settings[GRPC_SENT_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        server->settings[GRPC_ACKED_SETTINGS]
                        [GRPC_CHTTP2_SETTINGS_INITIAL_WINDOW_SIZE],
        client_stream ? client_stream->flow_controlled_buffer.length : 0,
        server_stream ? server_stream->flow_controlled_buffer.length : 0);
  }

  void Step(bool update_stats) {
    grpc_core::ExecCtx exec_ctx;
    inc_time();
    size_t client_backlog =
        grpc_trickle_endpoint_trickle(endpoint_pair_.client);
    size_t server_backlog =
        grpc_trickle_endpoint_trickle(endpoint_pair_.server);

    if (update_stats) {
      UpdateStats(reinterpret_cast<grpc_chttp2_transport*>(client_transport_),
                  &client_stats_, client_backlog);
      UpdateStats(reinterpret_cast<grpc_chttp2_transport*>(server_transport_),
                  &server_stats_, server_backlog);
    }
  }

 private:
  grpc_passthru_endpoint_stats* stats_;
  struct Stats {
    int streams_stalled_due_to_stream_flow_control = 0;
    int streams_stalled_due_to_transport_flow_control = 0;
  };
  Stats client_stats_;
  Stats server_stats_;
  std::unique_ptr<std::ofstream> log_;
  gpr_timespec start_ = gpr_now(GPR_CLOCK_MONOTONIC);

  static grpc_endpoint_pair MakeEndpoints(size_t kilobits,
                                          grpc_passthru_endpoint_stats* stats) {
    grpc_endpoint_pair p;
    grpc_passthru_endpoint_create(&p.client, &p.server,
                                  LibraryInitializer::get().rq(), stats);
    double bytes_per_second = 125.0 * kilobits;
    p.client = grpc_trickle_endpoint_create(p.client, bytes_per_second);
    p.server = grpc_trickle_endpoint_create(p.server, bytes_per_second);
    return p;
  }

  void UpdateStats(grpc_chttp2_transport* t, Stats* s,
                   size_t backlog) GPR_ATTRIBUTE_NO_TSAN {
    if (backlog == 0) {
      if (t->lists[GRPC_CHTTP2_LIST_STALLED_BY_STREAM].head != nullptr) {
        s->streams_stalled_due_to_stream_flow_control++;
      }
      if (t->lists[GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT].head != nullptr) {
        s->streams_stalled_due_to_transport_flow_control++;
      }
    }
  }
};

static void TrickleCQNext(TrickledCHTTP2* fixture, void** t, bool* ok,
                          int64_t iteration) {
  while (true) {
    fixture->Log(iteration);
    switch (
        fixture->cq()->AsyncNext(t, ok, gpr_inf_past(GPR_CLOCK_MONOTONIC))) {
      case CompletionQueue::TIMEOUT:
        fixture->Step(iteration != -1);
        break;
      case CompletionQueue::SHUTDOWN:
        GPR_ASSERT(false);
        break;
      case CompletionQueue::GOT_EVENT:
        return;
    }
  }
}

static void BM_PumpStreamServerToClient_Trickle(benchmark::State& state) {
  EchoTestService::AsyncService service;
  std::unique_ptr<TrickledCHTTP2> fixture(new TrickledCHTTP2(
      &service, true, state.range(0) /* req_size */,
      state.range(0) /* resp_size */, state.range(1) /* bw in kbit/s */,
      grpc_passthru_endpoint_stats_create()));
  {
    EchoResponse send_response;
    EchoResponse recv_response;
    if (state.range(0) > 0) {
      send_response.set_message(std::string(state.range(0), 'a'));
    }
    Status recv_status;
    ServerContext svr_ctx;
    ServerAsyncReaderWriter<EchoResponse, EchoRequest> response_rw(&svr_ctx);
    service.RequestBidiStream(&svr_ctx, &response_rw, fixture->cq(),
                              fixture->cq(), tag(0));
    std::unique_ptr<EchoTestService::Stub> stub(
        EchoTestService::NewStub(fixture->channel()));
    ClientContext cli_ctx;
    auto request_rw = stub->AsyncBidiStream(&cli_ctx, fixture->cq(), tag(1));
    int need_tags = (1 << 0) | (1 << 1);
    void* t;
    bool ok;
    while (need_tags) {
      TrickleCQNext(fixture.get(), &t, &ok, -1);
      GPR_ASSERT(ok);
      int i = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
    request_rw->Read(&recv_response, tag(0));
    auto inner_loop = [&](bool in_warmup) {
      GPR_TIMER_SCOPE("BenchmarkCycle", 0);
      response_rw.Write(send_response, tag(1));
      while (true) {
        TrickleCQNext(fixture.get(), &t, &ok,
                      in_warmup ? -1 : state.iterations());
        if (t == tag(0)) {
          request_rw->Read(&recv_response, tag(0));
        } else if (t == tag(1)) {
          break;
        } else {
          GPR_ASSERT(false);
        }
      }
    };
    gpr_timespec warmup_start = gpr_now(GPR_CLOCK_MONOTONIC);
    for (int i = 0; i < GPR_MAX(absl::GetFlag(FLAGS_warmup_iterations),
                                absl::GetFlag(FLAGS_warmup_megabytes) * 1024 *
                                    1024 / (14 + state.range(0)));
         i++) {
      inner_loop(true);
      if (gpr_time_cmp(gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), warmup_start),
                       gpr_time_from_seconds(
                           absl::GetFlag(FLAGS_warmup_max_time_seconds),
                           GPR_TIMESPAN)) > 0) {
        break;
      }
    }
    while (state.KeepRunning()) {
      inner_loop(false);
    }
    response_rw.Finish(Status::OK, tag(1));
    grpc::Status status;
    request_rw->Finish(&status, tag(2));
    need_tags = (1 << 0) | (1 << 1) | (1 << 2);
    while (need_tags) {
      TrickleCQNext(fixture.get(), &t, &ok, -1);
      if (t == tag(0) && ok) {
        request_rw->Read(&recv_response, tag(0));
        continue;
      }
      int i = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(need_tags & (1 << i));
      need_tags &= ~(1 << i);
    }
  }
  fixture->Finish(state);
  fixture.reset();
  state.SetBytesProcessed(state.range(0) * state.iterations());
}

static void StreamingTrickleArgs(benchmark::internal::Benchmark* b) {
  for (int i = 1; i <= 128 * 1024 * 1024; i *= 8) {
    for (int j = 64; j <= 128 * 1024 * 1024; j *= 8) {
      double expected_time =
          static_cast<double>(14 + i) / (125.0 * static_cast<double>(j));
      if (expected_time > 2.0) continue;
      b->Args({i, j});
    }
  }
}
BENCHMARK(BM_PumpStreamServerToClient_Trickle)->Apply(StreamingTrickleArgs);

static void BM_PumpUnbalancedUnary_Trickle(benchmark::State& state) {
  EchoTestService::AsyncService service;
  std::unique_ptr<TrickledCHTTP2> fixture(new TrickledCHTTP2(
      &service, false, state.range(0) /* req_size */,
      state.range(1) /* resp_size */, state.range(2) /* bw in kbit/s */,
      grpc_passthru_endpoint_stats_create()));
  EchoRequest send_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  if (state.range(0) > 0) {
    send_request.set_message(std::string(state.range(0), 'a'));
  }
  if (state.range(1) > 0) {
    send_response.set_message(std::string(state.range(1), 'a'));
  }
  Status recv_status;
  struct ServerEnv {
    ServerContext ctx;
    EchoRequest recv_request;
    grpc::ServerAsyncResponseWriter<EchoResponse> response_writer;
    ServerEnv() : response_writer(&ctx) {}
  };
  uint8_t server_env_buffer[2 * sizeof(ServerEnv)];
  ServerEnv* server_env[2] = {
      reinterpret_cast<ServerEnv*>(server_env_buffer),
      reinterpret_cast<ServerEnv*>(server_env_buffer + sizeof(ServerEnv))};
  new (server_env[0]) ServerEnv;
  new (server_env[1]) ServerEnv;
  service.RequestEcho(&server_env[0]->ctx, &server_env[0]->recv_request,
                      &server_env[0]->response_writer, fixture->cq(),
                      fixture->cq(), tag(0));
  service.RequestEcho(&server_env[1]->ctx, &server_env[1]->recv_request,
                      &server_env[1]->response_writer, fixture->cq(),
                      fixture->cq(), tag(1));
  std::unique_ptr<EchoTestService::Stub> stub(
      EchoTestService::NewStub(fixture->channel()));
  auto inner_loop = [&](bool in_warmup) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    recv_response.Clear();
    ClientContext cli_ctx;
    std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
        stub->AsyncEcho(&cli_ctx, send_request, fixture->cq()));
    void* t;
    bool ok;
    response_reader->Finish(&recv_response, &recv_status, tag(4));
    TrickleCQNext(fixture.get(), &t, &ok, in_warmup ? -1 : state.iterations());
    GPR_ASSERT(ok);
    GPR_ASSERT(t == tag(0) || t == tag(1));
    intptr_t slot = reinterpret_cast<intptr_t>(t);
    ServerEnv* senv = server_env[slot];
    senv->response_writer.Finish(send_response, Status::OK, tag(3));
    for (int i = (1 << 3) | (1 << 4); i != 0;) {
      TrickleCQNext(fixture.get(), &t, &ok,
                    in_warmup ? -1 : state.iterations());
      GPR_ASSERT(ok);
      int tagnum = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(i & (1 << tagnum));
      i -= 1 << tagnum;
    }
    GPR_ASSERT(recv_status.ok());

    senv->~ServerEnv();
    senv = new (senv) ServerEnv();
    service.RequestEcho(&senv->ctx, &senv->recv_request, &senv->response_writer,
                        fixture->cq(), fixture->cq(), tag(slot));
  };
  gpr_timespec warmup_start = gpr_now(GPR_CLOCK_MONOTONIC);
  for (int i = 0; i < GPR_MAX(absl::GetFlag(FLAGS_warmup_iterations),
                              absl::GetFlag(FLAGS_warmup_megabytes) * 1024 *
                                  1024 / (14 + state.range(0)));
       i++) {
    inner_loop(true);
    if (gpr_time_cmp(
            gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), warmup_start),
            gpr_time_from_seconds(absl::GetFlag(FLAGS_warmup_max_time_seconds),
                                  GPR_TIMESPAN)) > 0) {
      break;
    }
  }
  while (state.KeepRunning()) {
    inner_loop(false);
  }
  fixture->Finish(state);
  fixture.reset();
  server_env[0]->~ServerEnv();
  server_env[1]->~ServerEnv();
  state.SetBytesProcessed(state.range(0) * state.iterations() +
                          state.range(1) * state.iterations());
}

static void UnaryTrickleArgs(benchmark::internal::Benchmark* b) {
  for (int bw = 64; bw <= 128 * 1024 * 1024; bw *= 16) {
    b->Args({1, 1, bw});
    for (int i = 64; i <= 128 * 1024 * 1024; i *= 64) {
      double expected_time =
          static_cast<double>(14 + i) / (125.0 * static_cast<double>(bw));
      if (expected_time > 2.0) continue;
      b->Args({i, 1, bw});
      b->Args({1, i, bw});
      b->Args({i, i, bw});
    }
  }
}
BENCHMARK(BM_PumpUnbalancedUnary_Trickle)->Apply(UnaryTrickleArgs);
}  // namespace testing
}  // namespace grpc

extern gpr_timespec (*gpr_now_impl)(gpr_clock_type clock_type);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  grpc_timer_manager_set_threading(false);
  gpr_now_impl = ::grpc::testing::fake_now;
  benchmark::RunTheBenchmarksNamespaced();
}
