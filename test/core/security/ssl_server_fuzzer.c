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
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/memory_counters.h"
#include "test/core/util/mock_endpoint.h"

bool squelch = true;
// ssl has an array of global gpr_mu's that are never released.
// Turning this on will fail the leak check.
bool leak_check = false;

static void discard_write(grpc_slice slice) {}

static void dont_log(gpr_log_func_args *args) {}

struct handshake_state {
  bool done_callback_called;
};

static void on_handshake_done(grpc_exec_ctx *exec_ctx, void *arg,
                              grpc_error *error) {
  grpc_handshaker_args *args = arg;
  struct handshake_state *state = args->user_data;
  GPR_ASSERT(state->done_callback_called == false);
  state->done_callback_called = true;
  // The fuzzer should not pass the handshake.
  GPR_ASSERT(error != GRPC_ERROR_NONE);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  struct grpc_memory_counters counters;
  if (squelch) gpr_set_log_function(dont_log);
  if (leak_check) grpc_memory_counters_init();
  grpc_init();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  grpc_resource_quota *resource_quota =
      grpc_resource_quota_create("ssl_server_fuzzer");
  grpc_endpoint *mock_endpoint =
      grpc_mock_endpoint_create(discard_write, resource_quota);
  grpc_resource_quota_unref_internal(&exec_ctx, resource_quota);

  grpc_mock_endpoint_put_read(
      &exec_ctx, mock_endpoint,
      grpc_slice_from_copied_buffer((const char *)data, size));

  // Load key pair and establish server SSL credentials.
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  grpc_slice ca_slice, cert_slice, key_slice;
  ca_slice = grpc_slice_from_static_string(test_root_cert);
  cert_slice = grpc_slice_from_static_string(test_server1_cert);
  key_slice = grpc_slice_from_static_string(test_server1_key);
  const char *ca_cert = (const char *)GRPC_SLICE_START_PTR(ca_slice);
  pem_key_cert_pair.private_key = (const char *)GRPC_SLICE_START_PTR(key_slice);
  pem_key_cert_pair.cert_chain = (const char *)GRPC_SLICE_START_PTR(cert_slice);
  grpc_server_credentials *creds = grpc_ssl_server_credentials_create(
      ca_cert, &pem_key_cert_pair, 1, 0, NULL);

  // Create security connector
  grpc_server_security_connector *sc = NULL;
  grpc_security_status status =
      grpc_server_credentials_create_security_connector(&exec_ctx, creds, &sc);
  GPR_ASSERT(status == GRPC_SECURITY_OK);
  gpr_timespec deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                       gpr_time_from_seconds(1, GPR_TIMESPAN));

  struct handshake_state state;
  state.done_callback_called = false;
  grpc_handshake_manager *handshake_mgr = grpc_handshake_manager_create();
  grpc_server_security_connector_add_handshakers(&exec_ctx, sc, handshake_mgr);
  grpc_handshake_manager_do_handshake(
      &exec_ctx, handshake_mgr, mock_endpoint, NULL /* channel_args */,
      deadline, NULL /* acceptor */, on_handshake_done, &state);
  grpc_exec_ctx_flush(&exec_ctx);

  // If the given string happens to be part of the correct client hello, the
  // server will wait for more data. Explicitly fail the server by shutting down
  // the endpoint.
  if (!state.done_callback_called) {
    grpc_endpoint_shutdown(
        &exec_ctx, mock_endpoint,
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Explicit close"));
    grpc_exec_ctx_flush(&exec_ctx);
  }

  GPR_ASSERT(state.done_callback_called);

  grpc_handshake_manager_destroy(&exec_ctx, handshake_mgr);
  GRPC_SECURITY_CONNECTOR_UNREF(&exec_ctx, &sc->base, "test");
  grpc_server_credentials_release(creds);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  grpc_slice_unref(ca_slice);
  grpc_exec_ctx_flush(&exec_ctx);

  grpc_shutdown();
  if (leak_check) {
    counters = grpc_memory_counters_snapshot();
    grpc_memory_counters_destroy();
    GPR_ASSERT(counters.total_size_relative == 0);
  }
  return 0;
}
