//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/lib/gprpp/global_config_generic.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/security_connector/ssl_utils_config.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static grpc_end2end_test_fixture
chttp2_create_fixture_hostname_verifier_cert_watcher(const grpc_channel_args*,
                                                     const grpc_channel_args*) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_secure_fixture_data* ffd = new fullstack_secure_fixture_data();
  memset(&f, 0, sizeof(f));
  ffd->localaddr = grpc_core::JoinHostPort("localhost", port);
  SetTlsVersion(ffd, SecurityPrimitives::TlsVersion::V_12);
  SetCertificateProvider(ffd, SecurityPrimitives::ProviderType::FILE_PROVIDER);
  SetCertificateVerifier(ffd,
                         SecurityPrimitives::VerifierType::HOSTNAME_VERIFIER);
  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  return f;
}

static grpc_end2end_test_config config = {
    // client: certificate watcher provider + hostname verifier
    // server: certificate watcher provider + sync external verifier
    // extra: TLS 1.2
    "chttp2/cert_watcher_provider_sync_verifier_tls1_2",
    kH2TLSFeatureMask,
    "foo.test.google.fr",
    chttp2_create_fixture_hostname_verifier_cert_watcher,
    chttp2_init_client,
    chttp2_init_server,
    chttp2_tear_down_secure_fullstack,
};

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  GPR_GLOBAL_CONFIG_SET(grpc_default_ssl_roots_file_path, CA_CERT_PATH);
  grpc_init();
  grpc_end2end_tests(argc, argv, config);
  grpc_shutdown();
  return 0;
}
