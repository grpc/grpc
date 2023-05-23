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
      const Json& /*config*/) const override {
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
      const Json& config_json) const override {
    auto config = LoadFromJson<TlsConfig>(config_json);
    GPR_ASSERT(config.ok());
    auto options = MakeRefCounted<grpc_tls_credentials_options>();
// FIXME:
#if 0
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
#endif
    return MakeRefCounted<TlsCredentials>(std::move(options));
  }

 private:
  struct TlsConfig {
    std::string certificate_file;
    std::string private_key_file;
    std::string ca_certificate_file;
    Duration refresh_interval;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TlsConfig>()
              .OptionalField("certificate_file", &TlsConfig::certificate_file)
              .OptionalField("private_key_file", &TlsConfig::private_key_file)
              .OptionalField("ca_certificate_file",
                             &TlsConfig::ca_certificate_file)
              .OptionalField("refresh_interval", &TlsConfig::refresh_interval)
              .Finish();
      return loader;
    }

    void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors) {
// FIXME: implement validation checks
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
      const Json& /*config*/) const override {
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
      const Json& /*config*/) const override {
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
