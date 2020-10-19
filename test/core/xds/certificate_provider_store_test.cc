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

#include <thread>

#include <gmock/gmock.h>

#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/certificate_provider_store.h"

#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class FakeCertificateProvider : public grpc_tls_certificate_provider {
 public:
  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    // never called
    GPR_ASSERT(0);
    return nullptr;
  }
};

class FakeCertificateProviderFactory1 : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    const char* name() const override { return "fake1"; }
  };

  const char* name() const override { return "fake1"; }

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& config_json,
                                  grpc_error** error) override {
    return MakeRefCounted<Config>();
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<CertificateProviderFactory::Config> config) override {
    return MakeRefCounted<FakeCertificateProvider>();
  }
};

class FakeCertificateProviderFactory2 : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    const char* name() const override { return "fake2"; }
  };

  const char* name() const override { return "fake2"; }

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& config_json,
                                  grpc_error** error) override {
    return MakeRefCounted<Config>();
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<CertificateProviderFactory::Config> config) override {
    return MakeRefCounted<FakeCertificateProvider>();
  }
};

TEST(CertificateProviderStoreTest, Basic) {
  // Set up factories. (Register only one of the factories.)
  auto* fake_factory_1 = new FakeCertificateProviderFactory1;
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      std::unique_ptr<CertificateProviderFactory>(fake_factory_1));
  auto fake_factory_2 = absl::make_unique<FakeCertificateProviderFactory2>();
  // Set up store
  CertificateProviderStore::PluginDefinitionMap map = {
      {"fake_plugin_1",
       {"fake1", fake_factory_1->CreateCertificateProviderConfig(Json::Object(),
                                                                 nullptr)}},
      {"fake_plugin_2",
       {"fake2", fake_factory_2->CreateCertificateProviderConfig(Json::Object(),
                                                                 nullptr)}},
      {"fake_plugin_3",
       {"fake1", fake_factory_1->CreateCertificateProviderConfig(Json::Object(),
                                                                 nullptr)}},
  };
  CertificateProviderStore store(std::move(map));
  // Test for creating certificate providers with known plugin configuration.
  auto cert_provider_1 = store.CreateOrGetCertificateProvider("fake_plugin_1");
  ASSERT_NE(cert_provider_1, nullptr);
  auto cert_provider_3 = store.CreateOrGetCertificateProvider("fake_plugin_3");
  ASSERT_NE(cert_provider_3, nullptr);
  // Test for creating certificate provider with known plugin configuration but
  // unregistered factory.
  ASSERT_EQ(store.CreateOrGetCertificateProvider("fake_plugin_2"), nullptr);
  // Test for creating certificate provider with unknown plugin configuration.
  ASSERT_EQ(store.CreateOrGetCertificateProvider("unknown"), nullptr);
  // Test for getting previously created certificate providers.
  ASSERT_EQ(store.CreateOrGetCertificateProvider("fake_plugin_1"),
            cert_provider_1);
  ASSERT_EQ(store.CreateOrGetCertificateProvider("fake_plugin_3"),
            cert_provider_3);
  // Release previously created certificate providers.
  cert_provider_1.reset();
  cert_provider_3.reset();
}

TEST(CertificateProviderStoreTest, Multithreaded) {
  auto fake_factory_1 = absl::make_unique<FakeCertificateProviderFactory1>();
  CertificateProviderStore::PluginDefinitionMap map = {
      {"fake_plugin_1",
       {"fake1", fake_factory_1->CreateCertificateProviderConfig(Json::Object(),
                                                                 nullptr)}}};
  CertificateProviderStore store(std::move(map));
  // Test concurrent `CreateOrGetCertificateProvider()` with the same key.
  std::vector<std::thread> threads;
  for (auto i = 0; i < 10000; i++) {
    threads.emplace_back([&store]() {
      ASSERT_NE(store.CreateOrGetCertificateProvider("fake_plugin_1"), nullptr);
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
