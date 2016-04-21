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

#include <grpc/support/alloc.h>
#include <grpc/support/cmdline.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "test/core/util/memory_counters.h"
#include "test/core/util/test_config.h"

static struct grpc_memory_counters counters, previous_counters;

static void show_counters() {
  gpr_log(GPR_INFO, "  actual memory allocated:       %zu",
          counters.total_size_relative);
  gpr_log(GPR_INFO, "  total memory allocated:        %zu",
          counters.total_size_absolute);
  gpr_log(GPR_INFO, "  current number of allocations: %zu",
          counters.total_allocs_relative);
  gpr_log(GPR_INFO, "  total number of allocations:   %zu",
          counters.total_allocs_absolute);
}

static void show_difference() {
  gpr_log(GPR_INFO, "  actual memory allocated:       %zi",
          counters.total_size_relative - previous_counters.total_size_relative);
  gpr_log(GPR_INFO, "  total memory allocated:        %zi",
          counters.total_size_absolute - previous_counters.total_size_absolute);
  gpr_log(
      GPR_INFO, "  current number of allocations: %zi",
      counters.total_allocs_relative - previous_counters.total_allocs_relative);
  gpr_log(
      GPR_INFO, "  total number of allocations:   %zi",
      counters.total_allocs_absolute - previous_counters.total_allocs_absolute);
}

static void memory_probe(const char *op) {
  gpr_log(GPR_INFO, "Client - Memory usage after %s:", op);
  counters = grpc_memory_counters_snapshot();
  show_counters();
  gpr_log(GPR_INFO, "Client - Difference since last probe:");
  show_difference();
  previous_counters = counters;
  gpr_log(GPR_INFO, "----------------");
}

int main(int argc, char **argv) {
  grpc_byte_buffer *the_buffer;
  grpc_channel *channel;
  grpc_completion_queue *cq;
  grpc_call *call;
  grpc_call_error error;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_byte_buffer *response_payload_recv = NULL;
  grpc_status_code status;
  char *details = NULL;
  size_t details_capacity = 0;
  char *fake_argv[1];
  int payload_size = 1;
  char *target = "localhost:8080";
  gpr_cmdline *cl;
  grpc_event event;

  grpc_memory_counters_init();
  previous_counters = grpc_memory_counters_snapshot();
  grpc_init();

  GPR_ASSERT(argc >= 1);
  fake_argv[0] = argv[0];
  grpc_test_init(1, fake_argv);

  cl = gpr_cmdline_create("memory test client");
  gpr_cmdline_add_int(cl, "payload_size", "Size of the payload to send",
                      &payload_size);
  gpr_cmdline_add_string(cl, "target", "Target host:port", &target);
  gpr_cmdline_parse(cl, argc, argv);
  gpr_cmdline_destroy(cl);
  memory_probe("grpc_init & command line");

  channel = grpc_insecure_channel_create(target, NULL, NULL);
  memory_probe("grpc_insecure_channel_create");

  cq = grpc_completion_queue_create(NULL);
  memory_probe("grpc_completion_queue_create");
  gpr_slice slice = gpr_slice_from_copied_string("x");
  the_buffer = grpc_raw_byte_buffer_create(&slice, (size_t)payload_size);
  memory_probe("grpc_raw_byte_buffer_create");

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  memory_probe("grpc_metadata_array_init*2");

  op = ops;

  memset(ops, 0, sizeof(ops));
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

  call = grpc_channel_create_call(channel, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                                  "/Reflector/reflectUnary", "localhost",
                                  gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  memory_probe("grpc_channel_create_call");
  error = grpc_call_start_batch(call, ops, (size_t)(op - ops), (void *)1, NULL);
  memory_probe("grpc_call_start_batch");
  GPR_ASSERT(error == GRPC_CALL_OK);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  memory_probe("grpc_completion_queue_next");
  grpc_call_destroy(call);
  gpr_free(details);
  memory_probe("grpc_call_destroy");
  grpc_byte_buffer_destroy(response_payload_recv);
  memory_probe("grpc_byte_buffer_destroy");

  grpc_channel_destroy(channel);
  memory_probe("grpc_channel_destroy");
  grpc_completion_queue_shutdown(cq);
  memory_probe("grpc_completion_queue_shutdown");
  do {
    event = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                       NULL);
  } while (event.type != GRPC_QUEUE_SHUTDOWN);
  memory_probe("grpc_completion_queue_next*n");
  grpc_completion_queue_destroy(cq);
  memory_probe("grpc_completion_queue_destroy");
  grpc_byte_buffer_destroy(the_buffer);
  memory_probe("grpc_byte_memory_buffer_destroy");
  gpr_slice_unref(slice);

  grpc_shutdown();
  memory_probe("grpc_shutdown");
  grpc_memory_counters_destroy();

  return 0;
}
