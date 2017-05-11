/*
 *
 * Copyright 2016, Google Inc.
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

/* Benchmark gRPC end2end in various configurations */

#include <benchmark/benchmark.h>
#include <gflags/gflags.h>
#include <fstream>
#include "src/core/lib/profiling/timers.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/fullstack_context_mutators.h"
#include "test/cpp/microbenchmarks/fullstack_fixtures.h"
extern "C" {
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "test/core/util/trickle_endpoint.h"
}

DEFINE_bool(log, false, "Log state to CSV files");
DEFINE_int32(
    warmup_megabytes, 1,
    "Number of megabytes to pump before collecting flow control stats");
DEFINE_int32(
    warmup_iterations, 100,
    "Number of iterations to run before collecting flow control stats");
DEFINE_int32(warmup_max_time_seconds, 10,
             "Maximum number of seconds to run warmup loop");

namespace grpc {
namespace testing {

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
                 size_t resp_size, size_t kilobits_per_second)
      : EndpointPairFixture(service, MakeEndpoints(kilobits_per_second),
                            FixtureConfiguration()) {
    if (FLAGS_log) {
      std::ostringstream fn;
      fn << "trickle." << (streaming ? "streaming" : "unary") << "." << req_size
         << "." << resp_size << "." << kilobits_per_second << ".csv";
      log_.reset(new std::ofstream(fn.str().c_str()));
      write_csv(log_.get(), "t", "iteration", "client_backlog",
                "server_backlog", "client_t_stall", "client_s_stall",
                "server_t_stall", "server_s_stall", "client_t_outgoing",
                "server_t_outgoing", "client_t_incoming", "server_t_incoming",
                "client_s_outgoing_delta", "server_s_outgoing_delta",
                "client_s_incoming_delta", "server_s_incoming_delta",
                "client_s_announce_window", "server_s_announce_window",
                "client_peer_iws", "client_local_iws", "client_sent_iws",
                "client_acked_iws", "server_peer_iws", "server_local_iws",
                "server_sent_iws", "server_acked_iws", "client_queued_bytes",
                "server_queued_bytes");
    }
  }

  void AddToLabel(std::ostream& out, benchmark::State& state) {
    out << " writes/iter:"
        << ((double)stats_.num_writes / (double)state.iterations())
        << " cli_transport_stalls/iter:"
        << ((double)
                client_stats_.streams_stalled_due_to_transport_flow_control /
            (double)state.iterations())
        << " cli_stream_stalls/iter:"
        << ((double)client_stats_.streams_stalled_due_to_stream_flow_control /
            (double)state.iterations())
        << " svr_transport_stalls/iter:"
        << ((double)
                server_stats_.streams_stalled_due_to_transport_flow_control /
            (double)state.iterations())
        << " svr_stream_stalls/iter:"
        << ((double)server_stats_.streams_stalled_due_to_stream_flow_control /
            (double)state.iterations());
  }

  void Log(int64_t iteration) {
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
        log_.get(), static_cast<double>(now.tv_sec) +
                        1e-9 * static_cast<double>(now.tv_nsec),
        iteration, grpc_trickle_get_backlog(endpoint_pair_.client),
        grpc_trickle_get_backlog(endpoint_pair_.server),
        client->lists[GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT].head != nullptr,
        client->lists[GRPC_CHTTP2_LIST_STALLED_BY_STREAM].head != nullptr,
        server->lists[GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT].head != nullptr,
        server->lists[GRPC_CHTTP2_LIST_STALLED_BY_STREAM].head != nullptr,
        client->outgoing_window, server->outgoing_window,
        client->incoming_window, server->incoming_window,
        client_stream ? client_stream->outgoing_window_delta : -1,
        server_stream ? server_stream->outgoing_window_delta : -1,
        client_stream ? client_stream->incoming_window_delta : -1,
        server_stream ? server_stream->incoming_window_delta : -1,
        client_stream ? client_stream->announce_window : -1,
        server_stream ? server_stream->announce_window : -1,
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
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    size_t client_backlog =
        grpc_trickle_endpoint_trickle(&exec_ctx, endpoint_pair_.client);
    size_t server_backlog =
        grpc_trickle_endpoint_trickle(&exec_ctx, endpoint_pair_.server);
    grpc_exec_ctx_finish(&exec_ctx);

    if (update_stats) {
      UpdateStats((grpc_chttp2_transport*)client_transport_, &client_stats_,
                  client_backlog);
      UpdateStats((grpc_chttp2_transport*)server_transport_, &server_stats_,
                  server_backlog);
    }
  }

 private:
  grpc_passthru_endpoint_stats stats_;
  struct Stats {
    int streams_stalled_due_to_stream_flow_control = 0;
    int streams_stalled_due_to_transport_flow_control = 0;
  };
  Stats client_stats_;
  Stats server_stats_;
  std::unique_ptr<std::ofstream> log_;
  gpr_timespec start_ = gpr_now(GPR_CLOCK_MONOTONIC);

  grpc_endpoint_pair MakeEndpoints(size_t kilobits) {
    grpc_endpoint_pair p;
    grpc_passthru_endpoint_create(&p.client, &p.server, Library::get().rq(),
                                  &stats_);
    double bytes_per_second = 125.0 * kilobits;
    p.client = grpc_trickle_endpoint_create(p.client, bytes_per_second);
    p.server = grpc_trickle_endpoint_create(p.server, bytes_per_second);
    return p;
  }

  void UpdateStats(grpc_chttp2_transport* t, Stats* s, size_t backlog) {
    if (backlog == 0) {
      if (t->lists[GRPC_CHTTP2_LIST_STALLED_BY_STREAM].head != NULL) {
        s->streams_stalled_due_to_stream_flow_control++;
      }
      if (t->lists[GRPC_CHTTP2_LIST_STALLED_BY_TRANSPORT].head != NULL) {
        s->streams_stalled_due_to_transport_flow_control++;
      }
    }
  }
};

// force library initialization
auto& force_library_initialization = Library::get();

static void TrickleCQNext(TrickledCHTTP2* fixture, void** t, bool* ok,
                          int64_t iteration) {
  while (true) {
    fixture->Log(iteration);
    switch (fixture->cq()->AsyncNext(
        t, ok, gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                            gpr_time_from_micros(100, GPR_TIMESPAN)))) {
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
      state.range(0) /* resp_size */, state.range(1) /* bw in kbit/s */));
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
      int i = (int)(intptr_t)t;
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
    for (int i = 0;
         i < GPR_MAX(FLAGS_warmup_iterations, FLAGS_warmup_megabytes * 1024 *
                                                  1024 / (14 + state.range(0)));
         i++) {
      inner_loop(true);
      if (gpr_time_cmp(gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), warmup_start),
                       gpr_time_from_seconds(FLAGS_warmup_max_time_seconds,
                                             GPR_TIMESPAN)) > 0) {
        break;
      }
    }
    while (state.KeepRunning()) {
      inner_loop(false);
    }
    response_rw.Finish(Status::OK, tag(1));
    need_tags = (1 << 0) | (1 << 1);
    while (need_tags) {
      TrickleCQNext(fixture.get(), &t, &ok, -1);
      int i = (int)(intptr_t)t;
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
      &service, true, state.range(0) /* req_size */,
      state.range(1) /* resp_size */, state.range(2) /* bw in kbit/s */));
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
    TrickleCQNext(fixture.get(), &t, &ok, state.iterations());
    GPR_ASSERT(ok);
    GPR_ASSERT(t == tag(0) || t == tag(1));
    intptr_t slot = reinterpret_cast<intptr_t>(t);
    ServerEnv* senv = server_env[slot];
    senv->response_writer.Finish(send_response, Status::OK, tag(3));
    response_reader->Finish(&recv_response, &recv_status, tag(4));
    for (int i = (1 << 3) | (1 << 4); i != 0;) {
      TrickleCQNext(fixture.get(), &t, &ok, state.iterations());
      GPR_ASSERT(ok);
      int tagnum = (int)reinterpret_cast<intptr_t>(t);
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
  for (int i = 0;
       i < GPR_MAX(FLAGS_warmup_iterations, FLAGS_warmup_megabytes * 1024 *
                                                1024 / (14 + state.range(0)));
       i++) {
    inner_loop(true);
    if (gpr_time_cmp(gpr_time_sub(gpr_now(GPR_CLOCK_MONOTONIC), warmup_start),
                     gpr_time_from_seconds(FLAGS_warmup_max_time_seconds,
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
  const int cli_1024k = 1024 * 1024;
  const int cli_32M = 32 * 1024 * 1024;
  const int svr_256k = 256 * 1024;
  const int svr_4M = 4 * 1024 * 1024;
  const int svr_64M = 64 * 1024 * 1024;
  for (int bw = 64; bw <= 128 * 1024 * 1024; bw *= 16) {
    b->Args({bw, cli_1024k, svr_256k});
    b->Args({bw, cli_1024k, svr_4M});
    b->Args({bw, cli_1024k, svr_64M});
    b->Args({bw, cli_32M, svr_256k});
    b->Args({bw, cli_32M, svr_4M});
    b->Args({bw, cli_32M, svr_64M});
  }
}
BENCHMARK(BM_PumpUnbalancedUnary_Trickle)->Apply(UnaryTrickleArgs);
}
}

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  ::google::ParseCommandLineFlags(&argc, &argv, false);
  ::benchmark::RunSpecifiedBenchmarks();
}
