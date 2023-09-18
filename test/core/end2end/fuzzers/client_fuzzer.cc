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

#include <stdint.h>
#include <string.h>

#include <initializer_list>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include <grpc/byte_buffer.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/event_string.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/util/mock_endpoint.h"

using ::grpc_event_engine::experimental::FuzzingEventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

bool squelch = true;
bool leak_check = true;

static void discard_write(grpc_slice /*slice*/) {}

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static void dont_log(gpr_log_func_args* /*args*/) {}

DEFINE_PROTO_FUZZER(const fuzzer_input::Msg& msg) {
  if (squelch && !grpc_core::GetEnv("GRPC_TRACE_FUZZER").has_value()) {
    gpr_set_log_function(dont_log);
  }
  grpc_event_engine::experimental::SetEventEngineFactory([]() {
    return std::make_unique<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), fuzzing_event_engine::Actions{});
  });
  auto engine =
      std::dynamic_pointer_cast<FuzzingEventEngine>(GetDefaultEventEngine());
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);

    grpc_resource_quota* resource_quota =
        grpc_resource_quota_create("context_list_test");
    grpc_endpoint* mock_endpoint = grpc_mock_endpoint_create(discard_write);
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_core::ChannelArgs args = grpc_core::CoreConfiguration::Get()
                                      .channel_args_preconditioning()
                                      .PreconditionChannelArgs(nullptr);
    grpc_transport* transport =
        grpc_create_chttp2_transport(args, mock_endpoint, true);
    grpc_resource_quota_unref(resource_quota);
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    auto channel_args =
        grpc_core::CoreConfiguration::Get()
            .channel_args_preconditioning()
            .PreconditionChannelArgs(nullptr)
            .SetIfUnset(GRPC_ARG_DEFAULT_AUTHORITY, "test-authority");
    auto channel = grpc_core::Channel::Create(
        "test-target", channel_args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
    grpc_slice host = grpc_slice_from_static_string("localhost");
    grpc_call* call =
        grpc_channel_create_call(channel->get()->c_ptr(), nullptr, 0, cq,
                                 grpc_slice_from_static_string("/foo"), &host,
                                 gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

    grpc_metadata_array initial_metadata_recv;
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_byte_buffer* response_payload_recv = nullptr;
    grpc_metadata_array trailing_metadata_recv;
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_status_code status;
    grpc_slice details = grpc_empty_slice();

    engine->Tick();

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

    if (msg.network_input().has_single_read_bytes()) {
      grpc_mock_endpoint_put_read(
          mock_endpoint, grpc_slice_from_copied_buffer(
                             msg.network_input().single_read_bytes().data(),
                             msg.network_input().single_read_bytes().size()));
    }

    grpc_event ev;
    while (true) {
      engine->Tick();
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
    engine->FuzzingDone();
    engine->Tick();
    if (requested_calls) {
      grpc_call_cancel(call, nullptr);
    }
    for (int i = 0; i < requested_calls; i++) {
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      if (ev.type != GRPC_OP_COMPLETE) {
        grpc_core::Crash(absl::StrFormat(
            "[%d/%d requested calls] Unexpected event type (expected "
            "COMPLETE): %s",
            i, requested_calls, grpc_event_string(&ev).c_str()));
      }
    }
    grpc_completion_queue_shutdown(cq);
    for (int i = 0; i < requested_calls; i++) {
      ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                      nullptr);
      if (ev.type != GRPC_QUEUE_SHUTDOWN) {
        grpc_core::Crash(
            absl::StrFormat("Unexpected event type (expected SHUTDOWN): %s",
                            grpc_event_string(&ev).c_str()));
      }
    }
    grpc_call_unref(call);
    grpc_completion_queue_destroy(cq);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
    grpc_slice_unref(details);
    if (response_payload_recv != nullptr) {
      grpc_byte_buffer_destroy(response_payload_recv);
    }
  }
  grpc_shutdown_blocking();
}
