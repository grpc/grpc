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

#include <grpc/credentials.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "fuzztest/fuzztest.h"
#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/util/notification.h"
#include "test/core/test_util/mock_endpoint.h"
#include "test/core/test_util/test_config.h"
#include "test/core/test_util/tls_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

using grpc_core::HandshakerArgs;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::GetDefaultEventEngine;

void SslServerTest(std::vector<uint8_t> buffer) {
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;

    auto engine = GetDefaultEventEngine();
    auto mock_endpoint_controller =
        grpc_event_engine::experimental::MockEndpointController::Create(engine);
    mock_endpoint_controller->TriggerReadEvent(
        grpc_event_engine::experimental::Slice::FromCopiedBuffer(
            reinterpret_cast<const char*>(buffer.data()), buffer.size()));
    mock_endpoint_controller->NoMoreReads();

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
    handshake_mgr->DoHandshake(grpc_core::OrphanablePtr<grpc_endpoint>(
                                   mock_endpoint_controller->TakeCEndpoint()),
                               channel_args, deadline, nullptr /* acceptor */,
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
}
FUZZ_TEST(MyTestSuite, SslServerTest);
