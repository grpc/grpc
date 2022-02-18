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

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "test/core/memory_usage/memstats.h"
#include "test/core/util/cmdline.h"
#include "test/core/util/test_config.h"

static grpc_channel* channel;
static grpc_completion_queue* cq;
static grpc_op metadata_ops[2];
static grpc_op status_ops[2];
static grpc_op snapshot_ops[6];
static grpc_op* op;

typedef struct {
  grpc_call* call;
  grpc_metadata_array initial_metadata_recv;
  grpc_status_code status;
  grpc_slice details;
  grpc_metadata_array trailing_metadata_recv;
} fling_call;

// Statically allocate call data structs. Enough to accommodate 100000 ping-pong
// calls and 1 extra for the snapshot calls.
static fling_call calls[100001];

static void* tag(intptr_t t) { return (void*)t; }

// A call is intentionally divided into two steps. First step is to initiate a
// call (i.e send and recv metadata). A call is outstanding after we initated,
// so we can measure the call memory usage.
static void init_ping_pong_request(int call_idx) {
  grpc_metadata_array_init(&calls[call_idx].initial_metadata_recv);

  memset(metadata_ops, 0, sizeof(metadata_ops));
  op = metadata_ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &calls[call_idx].initial_metadata_recv;
  op++;

  grpc_slice hostname = grpc_slice_from_static_string("localhost");
  calls[call_idx].call = grpc_channel_create_call(
      channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/Reflector/reflectUnary"), &hostname,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(calls[call_idx].call,
                                                   metadata_ops,
                                                   (size_t)(op - metadata_ops),
                                                   tag(call_idx), nullptr));
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
}

// Second step is to finish the call (i.e recv status) and destroy the call.
static void finish_ping_pong_request(int call_idx) {
  grpc_metadata_array_init(&calls[call_idx].trailing_metadata_recv);

  memset(status_ops, 0, sizeof(status_ops));
  op = status_ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &calls[call_idx].trailing_metadata_recv;
  op->data.recv_status_on_client.status = &calls[call_idx].status;
  op->data.recv_status_on_client.status_details = &calls[call_idx].details;
  op++;

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(calls[call_idx].call,
                                                   status_ops,
                                                   (size_t)(op - status_ops),
                                                   tag(call_idx), nullptr));
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  grpc_metadata_array_destroy(&calls[call_idx].initial_metadata_recv);
  grpc_metadata_array_destroy(&calls[call_idx].trailing_metadata_recv);
  grpc_slice_unref(calls[call_idx].details);
  grpc_call_unref(calls[call_idx].call);
  calls[call_idx].call = nullptr;
}

static MemStats send_snapshot_request(int call_idx, grpc_slice call_type) {
  grpc_metadata_array_init(&calls[call_idx].initial_metadata_recv);
  grpc_metadata_array_init(&calls[call_idx].trailing_metadata_recv);

  grpc_byte_buffer* response_payload_recv = nullptr;
  memset(snapshot_ops, 0, sizeof(snapshot_ops));
  op = snapshot_ops;

  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &calls[call_idx].initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &response_payload_recv;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata =
      &calls[call_idx].trailing_metadata_recv;
  op->data.recv_status_on_client.status = &calls[call_idx].status;
  op->data.recv_status_on_client.status_details = &calls[call_idx].details;
  op++;

  grpc_slice hostname = grpc_slice_from_static_string("localhost");
  calls[call_idx].call = grpc_channel_create_call(
      channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq, call_type, &hostname,
      gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(calls[call_idx].call,
                                                   snapshot_ops,
                                                   (size_t)(op - snapshot_ops),
                                                   (void*)nullptr, nullptr));
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_byte_buffer_reader reader;
  grpc_byte_buffer_reader_init(&reader, response_payload_recv);
  grpc_slice response = grpc_byte_buffer_reader_readall(&reader);
  MemStats snapshot =
      *reinterpret_cast<MemStats*>(GRPC_SLICE_START_PTR(response));

  grpc_metadata_array_destroy(&calls[call_idx].initial_metadata_recv);
  grpc_metadata_array_destroy(&calls[call_idx].trailing_metadata_recv);
  grpc_slice_unref(response);
  grpc_byte_buffer_reader_destroy(&reader);
  grpc_byte_buffer_destroy(response_payload_recv);
  grpc_slice_unref(calls[call_idx].details);
  calls[call_idx].details = grpc_empty_slice();
  grpc_call_unref(calls[call_idx].call);
  calls[call_idx].call = nullptr;

  return snapshot;
}

int main(int argc, char** argv) {
  grpc_slice slice = grpc_slice_from_copied_string("x");
  char* fake_argv[1];

  const char* target = "localhost:443";
  gpr_cmdline* cl;
  grpc_event event;

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc::testing::TestEnvironment env(1, fake_argv);

  grpc_init();

  int warmup_iterations = 100;
  int benchmark_iterations = 1000;

  cl = gpr_cmdline_create("memory profiling client");
  gpr_cmdline_add_string(cl, "target", "Target host:port", &target);
  gpr_cmdline_add_int(cl, "warmup", "Warmup iterations", &warmup_iterations);
  gpr_cmdline_add_int(cl, "benchmark", "Benchmark iterations",
                      &benchmark_iterations);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);

  for (size_t k = 0; k < GPR_ARRAY_SIZE(calls); k++) {
    calls[k].details = grpc_empty_slice();
  }

  cq = grpc_completion_queue_create_for_next(nullptr);

  MemStats client_channel_start = MemStats::Snapshot();
  channel =
      grpc_channel_create(target, grpc_insecure_credentials_create(), nullptr);

  int call_idx = 0;

  MemStats before_server_create = send_snapshot_request(
      0, grpc_slice_from_static_string("Reflector/GetBeforeSvrCreation"));
  MemStats after_server_create = send_snapshot_request(
      0, grpc_slice_from_static_string("Reflector/GetAfterSvrCreation"));

  // warmup period
  for (int i = 0; i < warmup_iterations; i++) {
    send_snapshot_request(
        0, grpc_slice_from_static_string("Reflector/SimpleSnapshot"));
  }

  for (call_idx = 0; call_idx < warmup_iterations; ++call_idx) {
    init_ping_pong_request(call_idx + 1);
  }

  MemStats server_benchmark_calls_start = send_snapshot_request(
      0, grpc_slice_from_static_string("Reflector/SimpleSnapshot"));

  MemStats client_benchmark_calls_start = MemStats::Snapshot();

  // benchmark period
  for (; call_idx < warmup_iterations + benchmark_iterations; ++call_idx) {
    init_ping_pong_request(call_idx + 1);
  }

  MemStats client_calls_inflight = MemStats::Snapshot();

  MemStats server_calls_inflight = send_snapshot_request(
      0, grpc_slice_from_static_string("Reflector/DestroyCalls"));

  do {
    event = grpc_completion_queue_next(
        cq,
        gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                     gpr_time_from_micros(10000, GPR_TIMESPAN)),
        nullptr);
  } while (event.type != GRPC_QUEUE_TIMEOUT);

  // second step - recv status and destroy call
  for (call_idx = 0; call_idx < warmup_iterations + benchmark_iterations;
       ++call_idx) {
    finish_ping_pong_request(call_idx + 1);
  }

  MemStats server_calls_end = send_snapshot_request(
      0, grpc_slice_from_static_string("Reflector/SimpleSnapshot"));

  MemStats client_channel_end = MemStats::Snapshot();

  grpc_channel_destroy(channel);
  grpc_completion_queue_shutdown(cq);

  do {
    event = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       nullptr);
  } while (event.type != GRPC_QUEUE_SHUTDOWN);
  grpc_slice_unref(slice);

  grpc_completion_queue_destroy(cq);
  grpc_shutdown_blocking();

  gpr_log(GPR_INFO, "---------client stats--------");
  gpr_log(GPR_INFO, "client call memory usage: %f bytes per call",
          static_cast<double>(client_calls_inflight.rss -
                              client_benchmark_calls_start.rss) /
              benchmark_iterations * 1024);

  gpr_log(GPR_INFO, "---------server stats--------");
  gpr_log(GPR_INFO, "server call memory usage: %f bytes per call",
          static_cast<double>(server_calls_inflight.rss -
                              server_benchmark_calls_start.rss) /
              benchmark_iterations * 1024);

  const char* csv_file = "memory_usage.csv";
  FILE* csv = fopen(csv_file, "w");
  if (csv) {
    char* env_build = gpr_getenv("BUILD_NUMBER");
    char* env_job = gpr_getenv("JOB_NAME");
    fprintf(csv, "%f,%zi,%zi,%f,%zi,%s,%s\n",
            static_cast<double>(client_calls_inflight.rss -
                                client_benchmark_calls_start.rss) /
                benchmark_iterations,
            client_channel_end.rss - client_channel_start.rss,
            after_server_create.rss - before_server_create.rss,
            static_cast<double>(server_calls_inflight.rss -
                                server_benchmark_calls_start.rss) /
                benchmark_iterations,
            server_calls_end.rss - after_server_create.rss,
            env_build == nullptr ? "" : env_build,
            env_job == nullptr ? "" : env_job);
    fclose(csv);
    gpr_log(GPR_INFO, "Summary written to %s", csv_file);
  }

  return 0;
}
