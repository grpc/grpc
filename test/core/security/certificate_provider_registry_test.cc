//
//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/certificate_provider/certificate_provider_registry.h"

#include <gmock/gmock.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class FakeCertificateProviderFactory1 : public CertificateProviderFactory {
 public:
  absl::string_view name() const override { return "fake1"; }

  RefCountedPtr<Config> CreateCertificateProviderConfig(
      const Json& /*config_json*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) override {
    return nullptr;
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<Config> /*config*/) override {
    return nullptr;
  }
};

class FakeCertificateProviderFactory2 : public CertificateProviderFactory {
 public:
  absl::string_view name() const override { return "fake2"; }

  RefCountedPtr<Config> CreateCertificateProviderConfig(
      const Json& /*config_json*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) override {
    return nullptr;
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<Config> /*config*/) override {
    return nullptr;
  }
};

TEST(CertificateProviderRegistryTest, Basic) {
  CertificateProviderRegistry::Builder b;
  auto* fake_factory_1 = new FakeCertificateProviderFactory1;
  auto* fake_factory_2 = new FakeCertificateProviderFactory2;
  b.RegisterCertificateProviderFactory(
      std::unique_ptr<CertificateProviderFactory>(fake_factory_1));
  b.RegisterCertificateProviderFactory(
      std::unique_ptr<CertificateProviderFactory>(fake_factory_2));
  auto r = b.Build();
  EXPECT_EQ(r.LookupCertificateProviderFactory("fake1"), fake_factory_1);
  EXPECT_EQ(r.LookupCertificateProviderFactory("fake2"), fake_factory_2);
  EXPECT_EQ(r.LookupCertificateProviderFactory("fake3"), nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
