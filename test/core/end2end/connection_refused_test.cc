//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <string.h>

#include <string>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static void run_test(bool wait_for_ready, bool use_service_config) {
  grpc_channel* chan;
  grpc_call* call;
  grpc_completion_queue* cq;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array trailing_metadata_recv;
  grpc_status_code status;
  grpc_slice details;

  gpr_log(GPR_INFO, "TEST: wait_for_ready=%d use_service_config=%d",
          wait_for_ready, use_service_config);

  grpc_init();

  grpc_metadata_array_init(&trailing_metadata_recv);

  cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_core::CqVerifier cqv(cq);

  // if using service config, create channel args
  grpc_channel_args* args = nullptr;
  if (use_service_config) {
    GPR_ASSERT(wait_for_ready);
    grpc_arg arg;
    arg.type = GRPC_ARG_STRING;
    arg.key = const_cast<char*>(GRPC_ARG_SERVICE_CONFIG);
    arg.value.string = const_cast<char*>(
        "{\n"
        "  \"methodConfig\": [ {\n"
        "    \"name\": [\n"
        "      { \"service\": \"service\", \"method\": \"method\" }\n"
        "    ],\n"
        "    \"waitForReady\": true\n"
        "  } ]\n"
        "}");
    args = grpc_channel_args_copy_and_add(args, &arg, 1);
  }

  // create a call, channel to a port which will refuse connection
  int port = grpc_pick_unused_port_or_die();
  std::string addr = grpc_core::JoinHostPort("127.0.0.1", port);
  gpr_log(GPR_INFO, "server: %s", addr.c_str());
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  chan = grpc_channel_create(addr.c_str(), creds, args);
  grpc_channel_credentials_release(creds);
  grpc_slice host = grpc_slice_from_static_string("nonexistant");
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(2);
  call =
      grpc_channel_create_call(chan, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/service/method"),
                               &host, deadline, nullptr);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = (wait_for_ready && !use_service_config)
                  ? GRPC_INITIAL_METADATA_WAIT_FOR_READY
                  : 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_call_start_batch(call, ops, (size_t)(op - ops),
                                   grpc_core::CqVerifier::tag(1), nullptr));
  // verify that all tags get completed
  cqv.Expect(grpc_core::CqVerifier::tag(1), true);
  cqv.Verify();

  if (wait_for_ready) {
    GPR_ASSERT(status == GRPC_STATUS_DEADLINE_EXCEEDED);
  } else {
    GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  }

  grpc_completion_queue_shutdown(cq);
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  grpc_completion_queue_destroy(cq);
  grpc_call_unref(call);
  grpc_channel_destroy(chan);

  grpc_slice_unref(details);
  grpc_metadata_array_destroy(&trailing_metadata_recv);

  {
    grpc_core::ExecCtx exec_ctx;
    if (args != nullptr) grpc_channel_args_destroy(args);
  }

  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  run_test(false /* wait_for_ready */, false /* use_service_config */);
  run_test(true /* wait_for_ready */, false /* use_service_config */);
  run_test(true /* wait_for_ready */, true /* use_service_config */);
  return 0;
}
