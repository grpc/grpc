//
//
// Copyright 2023 gRPC authors.
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

#include <chrono>

#include <gtest/gtest.h>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_crl_provider.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_crl_provider.h"
#include "test/core/util/test_config.h"

static constexpr absl::string_view kCrlDirectory =
    "test/core/tsi/test_creds/crl_data/crls";

using ::grpc_core::experimental::CertificateInfoImpl;
using ::grpc_core::experimental::Crl;
using ::grpc_core::experimental::CrlProvider;

namespace grpc_core {
namespace testing {

TEST(DirectoryReloaderCrlProviderTestNoFixture, Construction) {
  auto provider = experimental::CreateDirectoryReloaderCrlProvider(
      kCrlDirectory, std::chrono::seconds(60), nullptr);
  ASSERT_TRUE(provider.ok()) << provider.status();
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
