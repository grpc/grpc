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

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/security/credentials/channel_creds_registry.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"  // IWYU pragma: keep
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"

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
    return LoadFromJson<RefCountedPtr<TlsConfig>>(config, args, errors);
  }

  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<ChannelCredsConfig> base_config) const override {
    auto* config = static_cast<const TlsConfig*>(base_config.get());
    auto options = MakeRefCounted<grpc_tls_credentials_options>();
    if (!config->certificate_file().empty() ||
        !config->ca_certificate_file().empty()) {
      options->set_certificate_provider(
          MakeRefCounted<FileWatcherCertificateProvider>(
              config->private_key_file(), config->certificate_file(),
              config->ca_certificate_file(),
              config->refresh_interval().millis() / GPR_MS_PER_SEC));
    }
    options->set_watch_root_cert(!config->ca_certificate_file().empty());
    options->set_watch_identity_pair(!config->certificate_file().empty());
    options->set_certificate_verifier(
        MakeRefCounted<HostNameCertificateVerifier>());
    return MakeRefCounted<TlsCredentials>(std::move(options));
  }

 private:
  // TODO(roth): It would be nice to share most of this config with the
  // xDS file watcher cert provider factory, but that would require
  // adding a dependency from lib to ext.
  class TlsConfig : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }

    bool Equals(const ChannelCredsConfig& other) const override {
      auto& o = static_cast<const TlsConfig&>(other);
      return certificate_file_ == o.certificate_file_ &&
             private_key_file_ == o.private_key_file_ &&
             ca_certificate_file_ == o.ca_certificate_file_ &&
             refresh_interval_ == o.refresh_interval_;
    }

    Json ToJson() const override {
      Json::Object obj;
      if (!certificate_file_.empty()) {
        obj["certificate_file"] = Json::FromString(certificate_file_);
      }
      if (!private_key_file_.empty()) {
        obj["private_key_file"] = Json::FromString(private_key_file_);
      }
      if (!ca_certificate_file_.empty()) {
        obj["ca_certificate_file"] = Json::FromString(ca_certificate_file_);
      }
      if (refresh_interval_ != kDefaultRefreshInterval) {
        obj["refresh_interval"] =
            Json::FromString(refresh_interval_.ToJsonString());
      }
      return Json::FromObject(std::move(obj));
    }

    const std::string& certificate_file() const { return certificate_file_; }
    const std::string& private_key_file() const { return private_key_file_; }
    const std::string& ca_certificate_file() const {
      return ca_certificate_file_;
    }
    Duration refresh_interval() const { return refresh_interval_; }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TlsConfig>()
              .OptionalField("certificate_file", &TlsConfig::certificate_file_)
              .OptionalField("private_key_file", &TlsConfig::private_key_file_)
              .OptionalField("ca_certificate_file",
                             &TlsConfig::ca_certificate_file_)
              .OptionalField("refresh_interval", &TlsConfig::refresh_interval_)
              .Finish();
      return loader;
    }

    void JsonPostLoad(const Json& json, const JsonArgs& /*args*/,
                      ValidationErrors* errors) {
      if ((json.object().find("certificate_file") == json.object().end()) !=
          (json.object().find("private_key_file") == json.object().end())) {
        errors->AddError(
            "fields \"certificate_file\" and \"private_key_file\" must be "
            "both set or both unset");
      }
    }

   private:
    static constexpr Duration kDefaultRefreshInterval = Duration::Minutes(10);

    std::string certificate_file_;
    std::string private_key_file_;
    std::string ca_certificate_file_;
    Duration refresh_interval_ = kDefaultRefreshInterval;
  };

  static absl::string_view Type() { return "tls"; }
};

constexpr Duration TlsChannelCredsFactory::TlsConfig::kDefaultRefreshInterval;

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
