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

#include <grpc/grpc.h>

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/support/string.h"
#include "test/core/util/memory_counters.h"
#include "test/core/util/test_config.h"

static grpc_channel *channel;
static grpc_completion_queue *cq;
static grpc_op metadata_ops[2];
static grpc_op status_ops[2];
static grpc_op snapshot_ops[6];
static grpc_op *op;

typedef struct {
  grpc_call *call;
  grpc_metadata_array initial_metadata_recv;
  grpc_status_code status;
  char *details;
  size_t details_capacity;
  grpc_metadata_array trailing_metadata_recv;
} fling_call;

// Statically allocate call data structs. Enough to accomodate 10000 ping-pong
// calls and 1 extra for the snapshot calls.
static fling_call calls[10001];

static void *tag(intptr_t t) { return (void *)t; }

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

  calls[call_idx].call = grpc_channel_create_call(
      channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq, "/Reflector/reflectUnary",
      "localhost", gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(calls[call_idx].call,
                                                   metadata_ops,
                                                   (size_t)(op - metadata_ops),
                                                   tag(call_idx), NULL));
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
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
  op->data.recv_status_on_client.status_details_capacity =
      &calls[call_idx].details_capacity;
  op++;

  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(calls[call_idx].call,
                                                   status_ops,
                                                   (size_t)(op - status_ops),
                                                   tag(call_idx), NULL));
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  grpc_metadata_array_destroy(&calls[call_idx].initial_metadata_recv);
  grpc_metadata_array_destroy(&calls[call_idx].trailing_metadata_recv);
  gpr_free(calls[call_idx].details);
  grpc_call_destroy(calls[call_idx].call);
  calls[call_idx].call = NULL;
}

static struct grpc_memory_counters send_snapshot_request(
    int call_idx, const char *call_type) {
  grpc_metadata_array_init(&calls[call_idx].initial_metadata_recv);
  grpc_metadata_array_init(&calls[call_idx].trailing_metadata_recv);

  grpc_byte_buffer *response_payload_recv = NULL;
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
  op->data.recv_status_on_client.status_details_capacity =
      &calls[call_idx].details_capacity;
  op++;

  calls[call_idx].call = grpc_channel_create_call(
      channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq, call_type, "localhost",
      gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(
                                 calls[call_idx].call, snapshot_ops,
                                 (size_t)(op - snapshot_ops), (void *)0, NULL));
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

  grpc_byte_buffer_reader reader;
  grpc_byte_buffer_reader_init(&reader, response_payload_recv);
  grpc_slice response = grpc_byte_buffer_reader_readall(&reader);

  struct grpc_memory_counters snapshot;
  snapshot.total_size_absolute =
      ((struct grpc_memory_counters *)GRPC_SLICE_START_PTR(response))
          ->total_size_absolute;
  snapshot.total_allocs_absolute =
      ((struct grpc_memory_counters *)GRPC_SLICE_START_PTR(response))
          ->total_allocs_absolute;
  snapshot.total_size_relative =
      ((struct grpc_memory_counters *)GRPC_SLICE_START_PTR(response))
          ->total_size_relative;
  snapshot.total_allocs_relative =
      ((struct grpc_memory_counters *)GRPC_SLICE_START_PTR(response))
          ->total_allocs_relative;

  grpc_metadata_array_destroy(&calls[call_idx].initial_metadata_recv);
  grpc_metadata_array_destroy(&calls[call_idx].trailing_metadata_recv);
  grpc_slice_unref(response);
  grpc_byte_buffer_reader_destroy(&reader);
  grpc_byte_buffer_destroy(response_payload_recv);
  gpr_free(calls[call_idx].details);
  calls[call_idx].details = NULL;
  calls[call_idx].details_capacity = 0;
  grpc_call_destroy(calls[call_idx].call);
  calls[call_idx].call = NULL;

  return snapshot;
}

int main(int argc, char **argv) {
  grpc_memory_counters_init();
  grpc_slice slice = grpc_slice_from_copied_string("x");
  char *fake_argv[1];

  char *target = "localhost:443";
  gpr_cmdline *cl;
  grpc_event event;

  grpc_init();

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  int warmup_iterations = 100;
  int benchmark_iterations = 1000;

  cl = gpr_cmdline_create("memory profiling client");
  gpr_cmdline_add_string(cl, "target", "Target host:port", &target);
  gpr_cmdline_add_int(cl, "warmup", "Warmup iterations", &warmup_iterations);
  gpr_cmdline_add_int(cl, "benchmark", "Benchmark iterations",
                      &benchmark_iterations);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);

  for (int k = 0; k < (int)(sizeof(calls) / sizeof(fling_call)); k++) {
    calls[k].details = NULL;
    calls[k].details_capacity = 0;
  }

  cq = grpc_completion_queue_create(NULL);

  struct grpc_memory_counters client_channel_start =
      grpc_memory_counters_snapshot();
  channel = grpc_insecure_channel_create(target, NULL, NULL);

  int call_idx = 0;

  struct grpc_memory_counters before_server_create =
      send_snapshot_request(0, "Reflector/GetBeforeSvrCreation");
  struct grpc_memory_counters after_server_create =
      send_snapshot_request(0, "Reflector/GetAfterSvrCreation");

  // warmup period
  for (call_idx = 0; call_idx < warmup_iterations; ++call_idx) {
    init_ping_pong_request(call_idx + 1);
  }

  struct grpc_memory_counters server_benchmark_calls_start =
      send_snapshot_request(0, "Reflector/SimpleSnapshot");

  struct grpc_memory_counters client_benchmark_calls_start =
      grpc_memory_counters_snapshot();

  // benchmark period
  for (; call_idx < warmup_iterations + benchmark_iterations; ++call_idx) {
    init_ping_pong_request(call_idx + 1);
  }

  struct grpc_memory_counters client_calls_inflight =
      grpc_memory_counters_snapshot();

  struct grpc_memory_counters server_calls_inflight =
      send_snapshot_request(0, "Reflector/DestroyCalls");

  do {
    event = grpc_completion_queue_next(
        cq, gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                         gpr_time_from_micros(10000, GPR_TIMESPAN)),
        NULL);
  } while (event.type != GRPC_QUEUE_TIMEOUT);

  // second step - recv status and destroy call
  for (call_idx = 0; call_idx < warmup_iterations + benchmark_iterations;
       ++call_idx) {
    finish_ping_pong_request(call_idx + 1);
  }

  struct grpc_memory_counters server_calls_end =
      send_snapshot_request(0, "Reflector/SimpleSnapshot");

  struct grpc_memory_counters client_channel_end =
      grpc_memory_counters_snapshot();

  grpc_channel_destroy(channel);
  grpc_completion_queue_shutdown(cq);

  do {
    event = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       NULL);
  } while (event.type != GRPC_QUEUE_SHUTDOWN);
  grpc_slice_unref(slice);

  grpc_completion_queue_destroy(cq);
  grpc_shutdown();

  gpr_log(GPR_INFO, "---------client stats--------");
  gpr_log(GPR_INFO, "client call memory usage: %f bytes per call",
          (double)(client_calls_inflight.total_size_relative -
                   client_benchmark_calls_start.total_size_relative) /
              benchmark_iterations);
  gpr_log(GPR_INFO, "client channel memory usage %zi bytes",
          client_channel_end.total_size_relative -
              client_channel_start.total_size_relative);

  gpr_log(GPR_INFO, "---------server stats--------");
  gpr_log(GPR_INFO, "server create: %zi bytes",
          after_server_create.total_size_relative -
              before_server_create.total_size_relative);
  gpr_log(GPR_INFO, "server call memory usage: %f bytes per call",
          (double)(server_calls_inflight.total_size_relative -
                   server_benchmark_calls_start.total_size_relative) /
              benchmark_iterations);
  gpr_log(GPR_INFO, "server channel memory usage %zi bytes",
          server_calls_end.total_size_relative -
              after_server_create.total_size_relative);

  grpc_memory_counters_destroy();
  return 0;
}
