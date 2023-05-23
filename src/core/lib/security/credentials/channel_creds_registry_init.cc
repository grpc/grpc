//
//
// Copyright 2022 gRPC authors.
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

#include <memory>

#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>

#include "src/core/ext/xds/certificate_provider_store.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"  // IWYU pragma: keep
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"

namespace grpc_core {

class GoogleDefaultChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "google_default"; }
  bool IsValidConfig(const Json& /*config*/, const JsonArgs& /*args*/,
                     ValidationErrors* /*errors*/) const override {
    return true;
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& /*config*/, CertificateProviderStore* /*store*/)
      const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_google_default_credentials_create(nullptr));
  }
};

class TlsChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "tls"; }

  bool IsValidConfig(const Json& config_json, const JsonArgs& args,
                     ValidationErrors* errors) const override {
    const size_t original_error_count = errors->size();
    LoadFromJson<TlsConfig>(config_json, args, errors);
    return original_error_count == errors->size();
  }

  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& config_json, CertificateProviderStore* store) const override {
    auto config = LoadFromJson<TlsConfig>(config_json);
    GPR_ASSERT(config.ok());
    auto options = MakeRefCounted<grpc_tls_credentials_options>();
    if (!config->cert_provider_instance.empty()) {
      auto cert_provider =
          store->CreateOrGetCertificateProvider(config->cert_provider_instance);
      GPR_ASSERT(cert_provider != nullptr);
      options->set_certificate_provider(std::move(cert_provider));
      if (config->root_cert_name.has_value()) {
        options->set_watch_root_cert(true);
        options->set_root_cert_name(std::move(*config->root_cert_name));
      }
      if (config->identity_cert_name.has_value()) {
        options->set_watch_identity_pair(true);
        options->set_identity_cert_name(std::move(*config->identity_cert_name));
      }
    }
    return MakeRefCounted<TlsCredentials>(std::move(options));
  }

 private:
  struct TlsConfig {
    std::string cert_provider_instance;
    absl::optional<std::string> root_cert_name;
    absl::optional<std::string> identity_cert_name;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TlsConfig>()
              .OptionalField("certificate_provider_instance",
                             &TlsConfig::cert_provider_instance)
              .OptionalField("root_certificate_name",
                             &TlsConfig::root_cert_name)
              .OptionalField("identity_certificate_name",
                             &TlsConfig::identity_cert_name)
              .Finish();
      return loader;
    }

    void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors) {
      if (!cert_provider_instance.empty()) {
        if (root_cert_name.has_value()) {
          ValidationErrors::ScopedField field(errors, ".root_cert_name");
          errors->AddError("must not be present if "
                           "certificate_provider_instance is not set");
        }
        if (identity_cert_name.has_value()) {
          ValidationErrors::ScopedField field(errors, ".identity_cert_name");
          errors->AddError("must not be present if "
                           "certificate_provider_instance is not set");
        }
      }
    }
  };
};

class InsecureChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "insecure"; }
  bool IsValidConfig(const Json& /*config*/, const JsonArgs& /*args*/,
                     ValidationErrors* /*errors*/) const override {
    return true;
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& /*config*/, CertificateProviderStore* /*store*/)
      const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  }
};

class FakeChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view creds_type() const override { return "fake"; }
  bool IsValidConfig(const Json& /*config*/, const JsonArgs& /*args*/,
                     ValidationErrors* /*errors*/) const override {
    return true;
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      const Json& /*config*/, CertificateProviderStore* /*store*/)
      const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }
};

void RegisterChannelDefaultCreds(CoreConfiguration::Builder* builder) {
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      std::make_unique<GoogleDefaultChannelCredsFactory>());
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      std::make_unique<TlsChannelCredsFactory>());
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      std::make_unique<InsecureChannelCredsFactory>());
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      std::make_unique<FakeChannelCredsFactory>());
}

}  // namespace grpc_core
