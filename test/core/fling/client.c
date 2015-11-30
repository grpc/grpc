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

#include <grpc/grpc.h>

#include <stdio.h>
#include <string.h>

#include <grpc/support/cmdline.h>
#include <grpc/support/histogram.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/profiling/timers.h"
#include "test/core/util/grpc_profiler.h"
#include "test/core/util/test_config.h"

static gpr_histogram *histogram;
static grpc_byte_buffer *the_buffer;
static grpc_channel *channel;
static grpc_completion_queue *cq;
static grpc_call *call;
static grpc_op ops[6];
static grpc_op stream_init_op;
static grpc_op stream_step_ops[2];
static grpc_metadata_array initial_metadata_recv;
static grpc_metadata_array trailing_metadata_recv;
static grpc_byte_buffer *response_payload_recv = NULL;
static grpc_status_code status;
static char *details = NULL;
static size_t details_capacity = 0;
static grpc_op *op;

static void init_ping_pong_request(void) {
  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);

  op = ops;

  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op++;
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message = the_buffer;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv;
  op++;
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message = &response_payload_recv;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op++;
}

static void step_ping_pong_request(void) {
  GPR_TIMER_BEGIN("ping_pong", 1);
  call = grpc_channel_create_call(channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                                  "/Reflector/reflectUnary", "localhost",
                                  gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                   (size_t)(op - ops),
                                                   (void *)1, NULL));
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  grpc_call_destroy(call);
  grpc_byte_buffer_destroy(response_payload_recv);
  call = NULL;
  GPR_TIMER_END("ping_pong", 1);
}

static void init_ping_pong_stream(void) {
  grpc_call_error error;
  call = grpc_channel_create_call(channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                                  "/Reflector/reflectStream", "localhost",
                                  gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  stream_init_op.op = GRPC_OP_SEND_INITIAL_METADATA;
  stream_init_op.data.send_initial_metadata.count = 0;
  error = grpc_call_start_batch(call, &stream_init_op, 1, (void *)1, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);

  grpc_metadata_array_init(&initial_metadata_recv);

  stream_step_ops[0].op = GRPC_OP_SEND_MESSAGE;
  stream_step_ops[0].data.send_message = the_buffer;
  stream_step_ops[1].op = GRPC_OP_RECV_MESSAGE;
  stream_step_ops[1].data.recv_message = &response_payload_recv;
}

static void step_ping_pong_stream(void) {
  grpc_call_error error;
  GPR_TIMER_BEGIN("ping_pong", 1);
  error = grpc_call_start_batch(call, stream_step_ops, 2, (void *)1, NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  grpc_byte_buffer_destroy(response_payload_recv);
  GPR_TIMER_END("ping_pong", 1);
}

static double now(void) {
  gpr_timespec tv = gpr_now(GPR_CLOCK_REALTIME);
  return 1e9 * (double)tv.tv_sec + tv.tv_nsec;
}

typedef struct {
  const char *name;
  void (*init)();
  void (*do_one_step)();
} scenario;

static const scenario scenarios[] = {
    {"ping-pong-request", init_ping_pong_request, step_ping_pong_request},
    {"ping-pong-stream", init_ping_pong_stream, step_ping_pong_stream},
};

int main(int argc, char **argv) {
  gpr_slice slice = gpr_slice_from_copied_string("x");
  double start, stop;
  unsigned i;

  char *fake_argv[1];

  int payload_size = 1;
  int secure = 0;
  char *target = "localhost:443";
  gpr_cmdline *cl;
  grpc_event event;
  char *scenario_name = "ping-pong-request";
  scenario sc = {NULL, NULL, NULL};

  gpr_timers_set_log_filename("latency_trace.fling_client.txt");

  grpc_init();

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  cl = gpr_cmdline_create("fling client");
  gpr_cmdline_add_int(cl, "payload_size", "Size of the payload to send",
                      &payload_size);
  gpr_cmdline_add_string(cl, "target", "Target host:port", &target);
  gpr_cmdline_add_flag(cl, "secure", "Run with security?", &secure);
  gpr_cmdline_add_string(cl, "scenario", "Scenario", &scenario_name);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);

  for (i = 0; i < GPR_ARRAY_SIZE(scenarios); i++) {
    if (0 == strcmp(scenarios[i].name, scenario_name)) {
      sc = scenarios[i];
    }
  }
  if (!sc.name) {
    fprintf(stderr, "unsupported scenario '%s'. Valid are:", scenario_name);
    for (i = 0; i < GPR_ARRAY_SIZE(scenarios); i++) {
      fprintf(stderr, " %s", scenarios[i].name);
    }
    return 1;
  }

  channel = grpc_insecure_channel_create(target, NULL, NULL);
  cq = grpc_completion_queue_create(NULL);
  the_buffer = grpc_raw_byte_buffer_create(&slice, (size_t)payload_size);
  histogram = gpr_histogram_create(0.01, 60e9);

  sc.init();

  for (i = 0; i < 1000; i++) {
    sc.do_one_step();
  }

  gpr_log(GPR_INFO, "start profiling");
  grpc_profiler_start("client.prof");
  for (i = 0; i < 100000; i++) {
    start = now();
    sc.do_one_step();
    stop = now();
    gpr_histogram_add(histogram, stop - start);
  }
  grpc_profiler_stop();

  if (call) {
    grpc_call_destroy(call);
  }

  grpc_channel_destroy(channel);
  grpc_completion_queue_shutdown(cq);
  do {
    event = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       NULL);
  } while (event.type != GRPC_QUEUE_SHUTDOWN);
  grpc_completion_queue_destroy(cq);
  grpc_byte_buffer_destroy(the_buffer);
  gpr_slice_unref(slice);

  gpr_log(GPR_INFO, "latency (50/95/99/99.9): %f/%f/%f/%f",
          gpr_histogram_percentile(histogram, 50),
          gpr_histogram_percentile(histogram, 95),
          gpr_histogram_percentile(histogram, 99),
          gpr_histogram_percentile(histogram, 99.9));
  gpr_histogram_destroy(histogram);

  grpc_shutdown();

  return 0;
}
