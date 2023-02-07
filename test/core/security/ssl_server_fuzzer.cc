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

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "test/core/util/mock_endpoint.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::GetDefaultEventEngine;

bool squelch = true;
// ssl has an array of global gpr_mu's that are never released.
// Turning this on will fail the leak check.
bool leak_check = false;

static void discard_write(grpc_slice /*slice*/) {}

static void dont_log(gpr_log_func_args* /*args*/) {}

struct handshake_state {
  bool done_callback_called;
};

static void on_handshake_done(void* arg, grpc_error_handle error) {
  grpc_core::HandshakerArgs* args =
      static_cast<grpc_core::HandshakerArgs*>(arg);
  struct handshake_state* state =
      static_cast<struct handshake_state*>(args->user_data);
  GPR_ASSERT(state->done_callback_called == false);
  state->done_callback_called = true;
  // The fuzzer should not pass the handshake.
  GPR_ASSERT(!error.ok());
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (squelch) gpr_set_log_function(dont_log);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;

    grpc_endpoint* mock_endpoint = grpc_mock_endpoint_create(discard_write);

    grpc_mock_endpoint_put_read(
        mock_endpoint, grpc_slice_from_copied_buffer((const char*)data, size));

    // Load key pair and establish server SSL credentials.
    grpc_slice ca_slice, cert_slice, key_slice;
    GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                 grpc_load_file(CA_CERT_PATH, 1, &ca_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "load_file", grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
    const char* ca_cert =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(ca_slice);
    const char* server_cert =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
    const char* server_key =
        reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key, server_cert};
    grpc_server_credentials* creds = grpc_ssl_server_credentials_create(
        ca_cert, &pem_key_cert_pair, 1, 0, nullptr);
    grpc_slice_unref(cert_slice);
    grpc_slice_unref(key_slice);
    grpc_slice_unref(ca_slice);

    // Create security connector
    grpc_core::RefCountedPtr<grpc_server_security_connector> sc =
        creds->create_security_connector(grpc_core::ChannelArgs());
    GPR_ASSERT(sc != nullptr);
    grpc_core::Timestamp deadline =
        grpc_core::Duration::Seconds(1) + grpc_core::Timestamp::Now();

    struct handshake_state state;
    state.done_callback_called = false;
    auto handshake_mgr =
        grpc_core::MakeRefCounted<grpc_core::HandshakeManager>();
    auto channel_args = grpc_core::ChannelArgs().SetObject<EventEngine>(
        GetDefaultEventEngine());
    sc->add_handshakers(channel_args, nullptr, handshake_mgr.get());
    handshake_mgr->DoHandshake(mock_endpoint, channel_args, deadline,
                               nullptr /* acceptor */, on_handshake_done,
                               &state);
    grpc_core::ExecCtx::Get()->Flush();

    // If the given string happens to be part of the correct client hello, the
    // server will wait for more data. Explicitly fail the server by shutting
    // down the endpoint.
    if (!state.done_callback_called) {
      grpc_endpoint_shutdown(mock_endpoint,
                             GRPC_ERROR_CREATE("Explicit close"));
      grpc_core::ExecCtx::Get()->Flush();
    }
    GPR_ASSERT(state.done_callback_called);

    sc.reset(DEBUG_LOCATION, "test");
    grpc_server_credentials_release(creds);
    grpc_core::ExecCtx::Get()->Flush();
  }

  grpc_shutdown();
  return 0;
}
