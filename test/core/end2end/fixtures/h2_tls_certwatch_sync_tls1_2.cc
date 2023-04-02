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

#include <functional>
#include <memory>
#include <string>

#include "absl/types/optional.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/h2_tls_common.h"
#include "test/core/util/test_config.h"

static CoreTestConfiguration config = {
    // client: certificate watcher provider + hostname verifier
    // server: certificate watcher provider + sync external verifier
    // extra: TLS 1.2
    "chttp2/cert_watcher_provider_sync_verifier_tls1_2",
    kH2TLSFeatureMask,
    "foo.test.google.fr",
    [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
      return std::make_unique<TlsFixture>(
          SecurityPrimitives::TlsVersion::V_12,
          SecurityPrimitives::ProviderType::FILE_PROVIDER,
          SecurityPrimitives::VerifierType::HOSTNAME_VERIFIER);
    },
};

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_core::ConfigVars::Overrides overrides;
  overrides.default_ssl_roots_file_path = CA_CERT_PATH;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_init();
  grpc_end2end_tests(argc, argv, config);
  grpc_shutdown();
  return 0;
}
