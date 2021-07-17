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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/util/mock_endpoint.h"

bool squelch = true;
bool leak_check = true;

static void discard_write(grpc_slice /*slice*/) {}

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static void dont_log(gpr_log_func_args* /*args*/) {}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  grpc_test_only_set_slice_hash_seed(0);
  if (squelch) gpr_set_log_function(dont_log);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    grpc_core::Executor::SetThreadingAll(false);

    grpc_resource_quota* resource_quota =
        grpc_resource_quota_create("client_fuzzer");
    grpc_endpoint* mock_endpoint =
        grpc_mock_endpoint_create(discard_write, resource_quota);
    grpc_resource_quota_unref_internal(resource_quota);

    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_transport* transport =
        grpc_create_chttp2_transport(nullptr, mock_endpoint, true);
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);

    grpc_arg authority_arg = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
        const_cast<char*>("test-authority"));
    grpc_channel_args* args =
        grpc_channel_args_copy_and_add(nullptr, &authority_arg, 1);
    grpc_channel* channel = grpc_channel_create(
        "test-target", args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
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
    grpc_call_error error =
        grpc_call_start_batch(call, ops, (size_t)(op - ops), tag(1), nullptr);
    int requested_calls = 1;
    GPR_ASSERT(GRPC_CALL_OK == error);

    grpc_mock_endpoint_put_read(
        mock_endpoint, grpc_slice_from_copied_buffer((const char*)data, size));

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
  return 0;
}
