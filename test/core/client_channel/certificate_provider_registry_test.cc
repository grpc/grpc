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

#include <gmock/gmock.h>

#include "src/core/ext/xds/certificate_provider_registry.h"

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class FakeCertificateProviderFactory1 : public CertificateProviderFactory {
 public:
  const char* name() const override { return "fake1"; }

  RefCountedPtr<Config> CreateCertificateProviderConfig(
      const Json& /*config_json*/, grpc_error_handle* /*error*/) override {
    return nullptr;
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<Config> /*config*/) override {
    return nullptr;
  }
};

class FakeCertificateProviderFactory2 : public CertificateProviderFactory {
 public:
  const char* name() const override { return "fake2"; }

  RefCountedPtr<Config> CreateCertificateProviderConfig(
      const Json& /*config_json*/, grpc_error_handle* /*error*/) override {
    return nullptr;
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<Config> /*config*/) override {
    return nullptr;
  }
};

TEST(CertificateProviderRegistryTest, Basic) {
  CertificateProviderRegistry::InitRegistry();
  auto* fake_factory_1 = new FakeCertificateProviderFactory1;
  auto* fake_factory_2 = new FakeCertificateProviderFactory2;
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      std::unique_ptr<CertificateProviderFactory>(fake_factory_1));
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      std::unique_ptr<CertificateProviderFactory>(fake_factory_2));
  EXPECT_EQ(
      CertificateProviderRegistry::LookupCertificateProviderFactory("fake1"),
      fake_factory_1);
  EXPECT_EQ(
      CertificateProviderRegistry::LookupCertificateProviderFactory("fake2"),
      fake_factory_2);
  EXPECT_EQ(
      CertificateProviderRegistry::LookupCertificateProviderFactory("fake3"),
      nullptr);
  CertificateProviderRegistry::ShutdownRegistry();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
