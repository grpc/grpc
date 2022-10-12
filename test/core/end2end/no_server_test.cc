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

#include <stdint.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/resolver/fake/fake_resolver.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/test_config.h"

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

void run_test(bool wait_for_ready) {
  gpr_log(GPR_INFO, "TEST: wait_for_ready=%d", wait_for_ready);

  grpc_init();

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_core::CqVerifier cqv(cq);

  grpc_core::RefCountedPtr<grpc_core::FakeResolverResponseGenerator>
      response_generator =
          grpc_core::MakeRefCounted<grpc_core::FakeResolverResponseGenerator>();
  grpc_arg arg = grpc_core::FakeResolverResponseGenerator::MakeChannelArg(
      response_generator.get());
  grpc_channel_args args = {1, &arg};

  /* create a call, channel to a non existant server */
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  grpc_channel* chan = grpc_channel_create("fake:nonexistant", creds, &args);
  grpc_channel_credentials_release(creds);
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(2);
  grpc_call* call = grpc_channel_create_call(
      chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/Foo"), nullptr, deadline, nullptr);

  grpc_op ops[6];
  memset(ops, 0, sizeof(ops));
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = wait_for_ready ? GRPC_INITIAL_METADATA_WAIT_FOR_READY : 0;
  op->reserved = nullptr;
  op++;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_status_code status;
  grpc_slice details;
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

  {
    grpc_core::ExecCtx exec_ctx;
    response_generator->SetFailure();
  }

  /* verify that all tags get completed */
  cqv.Expect(tag(1), true);
  cqv.Verify();

  gpr_log(GPR_INFO, "call status: %d", status);
  if (wait_for_ready) {
    GPR_ASSERT(status == GRPC_STATUS_DEADLINE_EXCEEDED);
  } else {
    GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  }

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);

  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
  grpc_call_unref(call);
  grpc_channel_destroy(chan);

  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  run_test(true /* wait_for_ready */);
  run_test(false /* wait_for_ready */);
  return 0;
}
