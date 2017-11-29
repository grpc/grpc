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

#include <grpc/grpc.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/memory_counters.h"
#include "test/core/util/mock_endpoint.h"

bool squelch = true;
bool leak_check = true;

static void discard_write(grpc_slice slice) {}

static void* tag(int n) { return (void*)(uintptr_t)n; }
static int detag(void* p) { return (int)(uintptr_t)p; }

static void dont_log(gpr_log_func_args* args) {}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  grpc_test_only_set_slice_hash_seed(0);
  struct grpc_memory_counters counters;
  if (squelch) gpr_set_log_function(dont_log);
  if (leak_check) grpc_memory_counters_init();
  grpc_init();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_executor_set_threading(&exec_ctx, false);

  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("server_fuzzer");
  grpc_endpoint* mock_endpoint =
      grpc_mock_endpoint_create(discard_write, resource_quota);
  grpc_resource_quota_unref_internal(&exec_ctx, resource_quota);
  grpc_mock_endpoint_put_read(
      &exec_ctx, mock_endpoint,
      grpc_slice_from_copied_buffer((const char*)data, size));

  grpc_server* server = grpc_server_create(nullptr, nullptr);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  // TODO(ctiller): add registered methods (one for POST, one for PUT)
  // void *registered_method =
  //    grpc_server_register_method(server, "/reg", NULL, 0);
  grpc_server_start(server);
  grpc_transport* transport =
      grpc_create_chttp2_transport(&exec_ctx, nullptr, mock_endpoint, 0);
  grpc_server_setup_transport(&exec_ctx, server, transport, nullptr, nullptr);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, nullptr);

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
  while (1) {
    grpc_exec_ctx_flush(&exec_ctx);
    ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                    nullptr);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        goto done;
      case GRPC_QUEUE_SHUTDOWN:
        break;
      case GRPC_OP_COMPLETE:
        switch (detag(ev.tag)) {
          case 1:
            requested_calls--;
            // TODO(ctiller): keep reading that call!
            break;
        }
    }
  }

done:
  if (call1 != nullptr) grpc_call_unref(call1);
  grpc_call_details_destroy(&call_details1);
  grpc_metadata_array_destroy(&request_metadata1);
  grpc_server_shutdown_and_notify(server, cq, tag(0xdead));
  grpc_server_cancel_all_calls(server);
  for (int i = 0; i <= requested_calls; i++) {
    ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  }
  grpc_completion_queue_shutdown(cq);
  for (int i = 0; i <= requested_calls; i++) {
    ev = grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME),
                                    nullptr);
    GPR_ASSERT(ev.type == GRPC_QUEUE_SHUTDOWN);
  }
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();
  if (leak_check) {
    counters = grpc_memory_counters_snapshot();
    grpc_memory_counters_destroy();
    GPR_ASSERT(counters.total_size_relative == 0);
  }
  return 0;
}
