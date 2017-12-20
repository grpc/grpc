/*
 *
 * Copyright 2015 gRPC authors.
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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

static void* tag(intptr_t i) { return (void*)i; }

int main(int argc, char** argv) {
  grpc_channel* chan;
  grpc_call* call;
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(2);
  grpc_completion_queue* cq;
  cq_verifier* cqv;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_slice details;

  grpc_test_init(argc, argv);
  grpc_init();

  grpc_metadata_array_init(&trailing_metadata_recv);

  cq = grpc_completion_queue_create_for_next(nullptr);
  cqv = cq_verifier_create(cq);

  /* create a call, channel to a non existant server */
  chan = grpc_insecure_channel_create("nonexistant:54321", nullptr, nullptr);
  grpc_slice host = grpc_slice_from_static_string("nonexistant");
  call = grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                                  grpc_slice_from_static_string("/Foo"), &host,
                                  deadline, nullptr);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                   (size_t)(op - ops), tag(1),
                                                   nullptr));
  /* verify that all tags get completed */
  CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
  cq_verify(cqv);

  GPR_ASSERT(status == GRPC_STATUS_DEADLINE_EXCEEDED);

  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN)
    ;
  grpc_completion_queue_destroy(cq);
  grpc_call_unref(call);
  grpc_channel_destroy(chan);
  cq_verifier_destroy(cqv);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);

  grpc_shutdown();

  return 0;
}
