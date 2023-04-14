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

#include <string>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/end2end/fuzzers/fuzzer_input.pb.h"
#include "test/core/util/mock_endpoint.h"

bool squelch = true;
bool leak_check = true;

static void discard_write(grpc_slice /*slice*/) {}

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static void dont_log(gpr_log_func_args* /*args*/) {}

DEFINE_PROTO_FUZZER(const fuzzer_input::Msg& msg) {
  if (squelch) gpr_set_log_function(dont_log);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);
    grpc_resource_quota* resource_quota =
        grpc_resource_quota_create("context_list_test");
    grpc_endpoint* mock_endpoint = grpc_mock_endpoint_create(discard_write);
    if (msg.network_input().has_single_read_bytes()) {
      grpc_mock_endpoint_put_read(
          mock_endpoint, grpc_slice_from_copied_buffer(
                             msg.network_input().single_read_bytes().data(),
                             msg.network_input().single_read_bytes().size()));
    }
    grpc_server* server = grpc_server_create(nullptr, nullptr);
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    // TODO(ctiller): add more registered methods (one for POST, one for PUT)
    grpc_server_register_method(server, "/reg", nullptr, {}, 0);
    grpc_server_start(server);
    grpc_core::ChannelArgs channel_args = grpc_core::CoreConfiguration::Get()
                                              .channel_args_preconditioning()
                                              .PreconditionChannelArgs(nullptr);
    grpc_transport* transport =
        grpc_create_chttp2_transport(channel_args, mock_endpoint, false);
    grpc_resource_quota_unref(resource_quota);
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "SetupTransport", grpc_core::Server::FromC(server)->SetupTransport(
                              transport, nullptr, channel_args, nullptr)));
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);

    grpc_call* call1 = nullptr;
    grpc_call_details call_details1;
    grpc_metadata_array request_metadata1;
    grpc_call_details_init(&call_details1);
    grpc_metadata_array_init(&request_metadata1);
    int requested_calls = 0;

    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_server_request_call(server, &call1, &call_details1,
                                        &request_metadata1, cq, cq, tag(1)));
    requested_calls++;

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
          if (ev.tag == tag(1)) {
            requested_calls--;
            // TODO(ctiller): keep reading that call!
          }
          break;
      }
    }

  done:
    if (call1 != nullptr) grpc_call_unref(call1);
    grpc_call_details_destroy(&call_details1);
    grpc_metadata_array_destroy(&request_metadata1);
    grpc_server_shutdown_and_notify(server, cq, tag(0xdead));
    grpc_server_cancel_all_calls(server);
    grpc_core::Timestamp deadline =
        grpc_core::Timestamp::Now() + grpc_core::Duration::Seconds(5);
    for (int i = 0; i <= requested_calls; i++) {
      // A single grpc_completion_queue_next might not be sufficient for getting
      // the tag from shutdown, because we might potentially get blocked by
      // an operation happening on the timer thread.
      // For example, the deadline timer might expire, leading to the timer
      // thread trying to cancel the RPC and thereby acquiring a few references
      // to the call. This will prevent the shutdown to complete till the timer
      // thread releases those references.
      // As a solution, we are going to keep performing a cq_next for a
      // liberal period of 5 seconds for the timer thread to complete its work.
      do {
        ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                        nullptr);
        grpc_core::ExecCtx::Get()->InvalidateNow();
      } while (ev.type != GRPC_OP_COMPLETE &&
               grpc_core::Timestamp::Now() < deadline);
      GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    }
    grpc_completion_queue_shutdown(cq);
    for (int i = 0; i <= requested_calls; i++) {
      do {
        ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                        nullptr);
        grpc_core::ExecCtx::Get()->InvalidateNow();
      } while (ev.type != GRPC_QUEUE_SHUTDOWN &&
               grpc_core::Timestamp::Now() < deadline);
      GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
    }
    grpc_server_destroy(server);
    grpc_completion_queue_destroy(cq);
  }
  grpc_shutdown();
}
