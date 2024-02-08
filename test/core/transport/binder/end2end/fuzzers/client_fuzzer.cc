// Copyright 2021 gRPC authors.
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

#include <thread>
#include <utility>

#include "absl/memory/memory.h"

#include <grpc/grpc.h>
#include <grpcpp/security/binder_security_policy.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/surface/channel.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/transport/binder/end2end/fuzzers/binder_transport_fuzzer.pb.h"
#include "test/core/transport/binder/end2end/fuzzers/fuzzer_utils.h"

bool squelch = true;
bool leak_check = true;

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static void dont_log(gpr_log_func_args*) {}

DEFINE_PROTO_FUZZER(const binder_transport_fuzzer::Input& input) {
  if (squelch) gpr_set_log_function(dont_log);
  grpc_init();
  {
    // Copied and modified from grpc/test/core/end2end/fuzzers/client_fuzzer.cc
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);

    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_core::Transport* client_transport =
        grpc_create_binder_transport_client(
            std::make_unique<grpc_binder::fuzzing::BinderForFuzzing>(
                input.incoming_parcels()),
            std::make_shared<
                grpc::experimental::binder::UntrustedSecurityPolicy>());
    grpc_arg authority_arg = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
        const_cast<char*>("test-authority"));
    grpc_channel_args* args =
        grpc_channel_args_copy_and_add(nullptr, &authority_arg, 1);
    auto channel_args = grpc_core::CoreConfiguration::Get()
                            .channel_args_preconditioning()
                            .PreconditionChannelArgs(args);
    auto channel =
        grpc_core::Channel::Create("test-target", channel_args,
                                   GRPC_CLIENT_DIRECT_CHANNEL, client_transport)
            ->release()
            ->c_ptr();
    grpc_channel_args_destroy(args);
    grpc_slice host = grpc_slice_from_static_string("localhost");
    grpc_call* call = grpc_channel_create_call(
        channel, nullptr, 0, cq, grpc_slice_from_static_string("/foo"), &host,
        gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    grpc_metadata_array initial_metadata_recv;
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_byte_buffer* response_payload_recv = nullptr;
    grpc_metadata_array trailing_metadata_recv;
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_status_code status;
    grpc_slice details = grpc_empty_slice();

    grpc_op ops[6];
    memset(ops, 0, sizeof(ops));
    grpc_op* op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    op->flags = 0;
    op->reserved = nullptr;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
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
    grpc_call_error error = grpc_call_start_batch(
        call, ops, static_cast<size_t>(op - ops), tag(1), nullptr);
    int requested_calls = 1;
    GPR_ASSERT(GRPC_CALL_OK == error);
    grpc_event ev;
    while (true) {
      grpc_core::ExecCtx::Get()->Flush();
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      switch (ev.type) {
        case GRPC_QUEUE_TIMEOUT:
          goto done;
        case GRPC_QUEUE_SHUTDOWN:
          break;
        case GRPC_OP_COMPLETE:
          requested_calls--;
          break;
      }
    }

  done:
    if (requested_calls) {
      grpc_call_cancel(call, nullptr);
    }
    grpc_binder::fuzzing::JoinFuzzingThread();
    for (int i = 0; i < requested_calls; i++) {
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    }
    grpc_completion_queue_shutdown(cq);
    for (int i = 0; i < requested_calls; i++) {
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
    }
    grpc_call_unref(call);
    grpc_completion_queue_destroy(cq);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
    grpc_slice_unref(details);
    grpc_channel_destroy(channel);
    if (response_payload_recv != nullptr) {
      grpc_byte_buffer_destroy(response_payload_recv);
    }
  }
  grpc_shutdown();
}
