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

#include "src/core/ext/xds/certificate_provider_store.h"

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class CertificateProviderStoreTest : public ::testing::Test {
 public:
  CertificateProviderStoreTest() { grpc_init(); }

  ~CertificateProviderStoreTest() override { grpc_shutdown_blocking(); }
};

class FakeCertificateProvider : public grpc_tls_certificate_provider {
 public:
  RefCountedPtr<grpc_tls_certificate_distributor> distributor() const override {
    // never called
    GPR_ASSERT(0);
    return nullptr;
  }

  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("fake");
    return kFactory.Create();
  }

 private:
  int CompareImpl(const grpc_tls_certificate_provider* other) const override {
    // TODO(yashykt): Maybe do something better here.
    return QsortCompare(static_cast<const grpc_tls_certificate_provider*>(this),
                        other);
  }
};

class FakeCertificateProviderFactory1 : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    const char* name() const override { return "fake1"; }

    std::string ToString() const override { return "{}"; }
  };

  const char* name() const override { return "fake1"; }

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& /*config_json*/,
                                  grpc_error_handle* /*error*/) override {
    return MakeRefCounted<Config>();
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<CertificateProviderFactory::Config> /*config*/) override {
    return MakeRefCounted<FakeCertificateProvider>();
  }
};

class FakeCertificateProviderFactory2 : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    const char* name() const override { return "fake2"; }

    std::string ToString() const override { return "{}"; }
  };

  const char* name() const override { return "fake2"; }

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& /*config_json*/,
                                  grpc_error_handle* /*error*/) override {
    return MakeRefCounted<Config>();
  }

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<CertificateProviderFactory::Config> /*config*/) override {
    return MakeRefCounted<FakeCertificateProvider>();
  }
};

TEST_F(CertificateProviderStoreTest, Basic) {
  // Set up factories. (Register only one of the factories.)
  auto* fake_factory_1 = new FakeCertificateProviderFactory1;
  CoreConfiguration::RunWithSpecialConfiguration(
      [=](CoreConfiguration::Builder* builder) {
        builder->certificate_provider_registry()
            ->RegisterCertificateProviderFactory(
                std::unique_ptr<CertificateProviderFactory>(fake_factory_1));
      },
      [=] {
        auto fake_factory_2 =
            std::make_unique<FakeCertificateProviderFactory2>();
        // Set up store
        CertificateProviderStore::PluginDefinitionMap map = {
            {"fake_plugin_1",
             {"fake1", fake_factory_1->CreateCertificateProviderConfig(
                           Json::FromObject({}), nullptr)}},
            {"fake_plugin_2",
             {"fake2", fake_factory_2->CreateCertificateProviderConfig(
                           Json::FromObject({}), nullptr)}},
            {"fake_plugin_3",
             {"fake1", fake_factory_1->CreateCertificateProviderConfig(
                           Json::FromObject({}), nullptr)}},
        };
        auto store = MakeOrphanable<CertificateProviderStore>(std::move(map));
        // Test for creating certificate providers with known plugin
        // configuration.
        auto cert_provider_1 =
            store->CreateOrGetCertificateProvider("fake_plugin_1");
        ASSERT_NE(cert_provider_1, nullptr);
        auto cert_provider_3 =
            store->CreateOrGetCertificateProvider("fake_plugin_3");
        ASSERT_NE(cert_provider_3, nullptr);
        // Test for creating certificate provider with known plugin
        // configuration but unregistered factory.
        ASSERT_EQ(store->CreateOrGetCertificateProvider("fake_plugin_2"),
                  nullptr);
        // Test for creating certificate provider with unknown plugin
        // configuration.
        ASSERT_EQ(store->CreateOrGetCertificateProvider("unknown"), nullptr);
        // Test for getting previously created certificate providers.
        ASSERT_EQ(store->CreateOrGetCertificateProvider("fake_plugin_1"),
                  cert_provider_1);
        ASSERT_EQ(store->CreateOrGetCertificateProvider("fake_plugin_3"),
                  cert_provider_3);
        // Release previously created certificate providers so that the store
        // outlasts the certificate providers.
        cert_provider_1.reset();
        cert_provider_3.reset();
      });
}

TEST_F(CertificateProviderStoreTest, Multithreaded) {
  auto* fake_factory_1 = new FakeCertificateProviderFactory1;
  CoreConfiguration::RunWithSpecialConfiguration(
      [=](CoreConfiguration::Builder* builder) {
        builder->certificate_provider_registry()
            ->RegisterCertificateProviderFactory(
                std::unique_ptr<CertificateProviderFactory>(fake_factory_1));
      },
      [=] {
        CertificateProviderStore::PluginDefinitionMap map = {
            {"fake_plugin_1",
             {"fake1", fake_factory_1->CreateCertificateProviderConfig(
                           Json::FromObject({}), nullptr)}}};
        auto store = MakeOrphanable<CertificateProviderStore>(std::move(map));
        // Test concurrent `CreateOrGetCertificateProvider()` with the same key.
        std::vector<std::thread> threads;
        threads.reserve(1000);
        for (auto i = 0; i < 1000; i++) {
          threads.emplace_back([&store]() {
            for (auto i = 0; i < 10; ++i) {
              ASSERT_NE(store->CreateOrGetCertificateProvider("fake_plugin_1"),
                        nullptr);
            }
          });
        }
        for (auto& thread : threads) {
          thread.join();
        }
      });
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
