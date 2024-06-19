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

#include "absl/log/check.h"
#include "absl/synchronization/notification.h"

#include <grpc/credentials.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "test/core/test_util/mock_endpoint.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

using grpc_core::HandshakerArgs;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::GetDefaultEventEngine;

bool squelch = true;
// ssl has an array of global gpr_mu's that are never released.
// Turning this on will fail the leak check.
bool leak_check = false;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (squelch) {
    grpc_disable_all_absl_logs();
  }
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;

    auto engine = GetDefaultEventEngine();
    grpc_endpoint* mock_endpoint = grpc_mock_endpoint_create(engine);

    grpc_mock_endpoint_put_read(
        mock_endpoint, grpc_slice_from_copied_buffer((const char*)data, size));
    grpc_mock_endpoint_finish_put_reads(mock_endpoint);

    // Load key pair and establish server SSL credentials.
    std::string ca_cert = grpc_core::testing::GetFileContents(CA_CERT_PATH);
    std::string server_cert =
        grpc_core::testing::GetFileContents(SERVER_CERT_PATH);
    std::string server_key =
        grpc_core::testing::GetFileContents(SERVER_KEY_PATH);
    grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key.c_str(),
                                                    server_cert.c_str()};
    grpc_server_credentials* creds = grpc_ssl_server_credentials_create(
        ca_cert.c_str(), &pem_key_cert_pair, 1, 0, nullptr);

    // Create security connector
    grpc_core::RefCountedPtr<grpc_server_security_connector> sc =
        creds->create_security_connector(grpc_core::ChannelArgs());
    CHECK(sc != nullptr);
    grpc_core::Timestamp deadline =
        grpc_core::Duration::Seconds(1) + grpc_core::Timestamp::Now();

    auto handshake_mgr =
        grpc_core::MakeRefCounted<grpc_core::HandshakeManager>();
    auto channel_args =
        grpc_core::ChannelArgs().SetObject<EventEngine>(std::move(engine));
    sc->add_handshakers(channel_args, nullptr, handshake_mgr.get());
    absl::Notification handshake_completed;
    handshake_mgr->DoHandshake(
        grpc_core::OrphanablePtr<grpc_endpoint>(mock_endpoint), channel_args,
        deadline, nullptr /* acceptor */,
        [&](absl::StatusOr<HandshakerArgs*> result) {
          // The fuzzer should not pass the handshake.
          CHECK(!result.ok());
          handshake_completed.Notify();
        });
    grpc_core::ExecCtx::Get()->Flush();

    // If the given string happens to be part of the correct client hello, the
    // server will wait for more data. Explicitly fail the server by shutting
    // down the handshake manager.
    if (!handshake_completed.WaitForNotificationWithTimeout(absl::Seconds(3))) {
      handshake_mgr->Shutdown(
          absl::DeadlineExceededError("handshake did not fail as expected"));
    }

    sc.reset(DEBUG_LOCATION, "test");
    grpc_server_credentials_release(creds);
    grpc_core::ExecCtx::Get()->Flush();
  }

  grpc_shutdown();
  return 0;
}
