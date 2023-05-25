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

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/time.h>

#include "src/core/ext/xds/file_watcher_certificate_provider_factory.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"  // IWYU pragma: keep
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"

namespace grpc_core {

class GoogleDefaultChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<ChannelCredsConfig> /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_google_default_credentials_create(nullptr));
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    Json ToJson() const override { return Json::FromObject({}); }
  };

  static absl::string_view Type() { return "google_default"; }
};

class TlsChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }

  RefCountedPtr<ChannelCredsConfig> ParseConfig(
      const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const override {
    RefCountedPtr<FileWatcherCertificateProviderFactory::Config>
        file_watcher_config;
    // The file watcher config generates an error if neither "certificate_file"
    // nor "ca_certificate_file" are present.  But in this case, we want
    // to allow that config, which means to use basic TLS with
    // system-wide root certs.
    if (config.object().find("certificate_file") != config.object().end() ||
        config.object().find("ca_certificate_file") != config.object().end()) {
      file_watcher_config = LoadFromJson<
          RefCountedPtr<FileWatcherCertificateProviderFactory::Config>>(
          config, args, errors);
    }
    return MakeRefCounted<TlsConfig>(std::move(file_watcher_config));
  }

  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<ChannelCredsConfig> base_config) const override {
    auto* config = static_cast<const TlsConfig*>(base_config.get())->config();
    auto options = MakeRefCounted<grpc_tls_credentials_options>();
    if (config != nullptr) {
      options->set_certificate_provider(
          MakeRefCounted<FileWatcherCertificateProvider>(
              config->private_key_file(), config->certificate_file(),
              config->ca_certificate_file(),
              config->refresh_interval().millis() / GPR_MS_PER_SEC));
      options->set_watch_root_cert(!config->ca_certificate_file().empty());
      options->set_watch_identity_pair(!config->certificate_file().empty());
    }
    return MakeRefCounted<TlsCredentials>(std::move(options));
  }

 private:
  class TlsConfig : public ChannelCredsConfig {
   public:
    explicit TlsConfig(
        RefCountedPtr<FileWatcherCertificateProviderFactory::Config>
            cert_provider_config)
        : cert_provider_config_(std::move(cert_provider_config)) {}

    absl::string_view type() const override { return Type(); }

    bool Equals(const ChannelCredsConfig& other) const override {
      auto& o = static_cast<const TlsConfig&>(other);
      // If one is null and the other isn't, they're not equal.
      if ((cert_provider_config_ == nullptr) !=
          (o.cert_provider_config_ == nullptr)) {
        return false;
      }
      // If they're both null, they're equal.
      if (cert_provider_config_ == nullptr) return true;
      // If neither one is null, compare them.
      return cert_provider_config_->Equals(*o.cert_provider_config_);
    }

    Json ToJson() const override {
      if (cert_provider_config_ == nullptr) return Json::FromObject({});
      return cert_provider_config_->ToJson();
    }

    const FileWatcherCertificateProviderFactory::Config* config() const {
      return cert_provider_config_.get();
    }

   private:
    RefCountedPtr<FileWatcherCertificateProviderFactory::Config>
        cert_provider_config_;
  };

  static absl::string_view Type() { return "tls"; }
};

class InsecureChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<ChannelCredsConfig> /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    Json ToJson() const override { return Json::FromObject({}); }
  };

  static absl::string_view Type() { return "insecure"; }
};

class FakeChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<ChannelCredsConfig> /*config*/) const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    Json ToJson() const override { return Json::FromObject({}); }
  };

  static absl::string_view Type() { return "fake"; }
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
