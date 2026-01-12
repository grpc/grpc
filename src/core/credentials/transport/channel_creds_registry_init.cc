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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/json.h>
#include <grpc/support/time.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "envoy/extensions/grpc_service/channel_credentials/tls/v3/tls_credentials.upb.h"
#include "envoy/extensions/grpc_service/channel_credentials/xds/v3/xds_credentials.upb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.upb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/transport/channel_creds_registry.h"
#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/credentials/transport/google_default/google_default_credentials.h"  // IWYU pragma: keep
#include "src/core/credentials/transport/tls/grpc_tls_certificate_provider.h"
#include "src/core/credentials/transport/tls/grpc_tls_credentials_options.h"
#include "src/core/credentials/transport/tls/tls_credentials.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/validation_errors.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class GoogleDefaultChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<const ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  absl::string_view proto_type() const override { return ProtoType(); }
  RefCountedPtr<const ChannelCredsConfig> ParseProto(
      absl::string_view /*serialized_config*/,
      const CertificateProviderStoreInterface::PluginDefinitionMap&
      /*certificate_provider_definitions*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<const ChannelCredsConfig> /*config*/,
      const CertificateProviderStoreInterface& /*certificate_provider_store*/)
      const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_google_default_credentials_create(nullptr, nullptr));
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    absl::string_view proto_type() const override { return ProtoType(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    std::string ToString() const override { return "{}"; }
  };

  static absl::string_view Type() { return "google_default"; }

  static absl::string_view ProtoType() {
    return "envoy.extensions.grpc_service.channel_credentials.google_default"
           ".v3.GoogleDefaultCredentials";
  }
};

class TlsChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }

  RefCountedPtr<const ChannelCredsConfig> ParseConfig(
      const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const override {
    return LoadFromJson<RefCountedPtr<TlsConfig>>(config, args, errors);
  }

  absl::string_view proto_type() const override { return ProtoType(); }

  RefCountedPtr<const ChannelCredsConfig> ParseProto(
      absl::string_view serialized_config,
      const CertificateProviderStoreInterface::PluginDefinitionMap&
          certificate_provider_definitions,
      ValidationErrors* errors) const override {
    return TlsConfig::ParseProto(serialized_config,
                                 certificate_provider_definitions, errors);
  }

  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<const ChannelCredsConfig> base_config,
      const CertificateProviderStoreInterface& certificate_provider_store)
      const override {
    auto* config = static_cast<const TlsConfig*>(base_config.get());
    auto options = MakeRefCounted<grpc_tls_credentials_options>();
    if (!config->root_certificate_provider().instance_name.empty()) {
// FIXME: make this work once https://github.com/grpc/grpc/pull/41088 is merged
// (or maybe need a layer of indirection anyway for the cert names?)
#if 0
      options->set_root_certificate_provider(
          certificate_provider_store.CreateOrGetCertificateProvider(
              config->root_certificate_provider().instance_name));
      // FIXME: figure out how to plumb certificate name
      if (!config->identity_certificate_provider().instance_name.empty()) {
        options->set_identity_certificate_provider(
            certificate_provider_store.CreateOrGetCertificateProvider(
                config->identity_certificate_provider().instance_name));
        // FIXME: figure out how to plumb certificate name
      }
#endif
    } else {
      if (!config->certificate_file().empty() ||
          !config->ca_certificate_file().empty()) {
        // TODO(gtcooke94): Expose the spiffe_bundle_map option in the XDS
        // bootstrap config to use here.
        options->set_certificate_provider(
            MakeRefCounted<FileWatcherCertificateProvider>(
                config->private_key_file(), config->certificate_file(),
                config->ca_certificate_file(), /*spiffe_bundle_map_file=*/"",
                config->refresh_interval().millis() / GPR_MS_PER_SEC));
      }
      options->set_watch_root_cert(!config->ca_certificate_file().empty());
      options->set_watch_identity_pair(!config->certificate_file().empty());
    }
    options->set_certificate_verifier(
        MakeRefCounted<HostNameCertificateVerifier>());
    return MakeRefCounted<TlsCredentials>(std::move(options));
  }

 private:
  // TODO(roth): This duplicates a bunch of code from the xDS bootstrap
  // parsing code and the CommonTlsContext parsing code.  When we have
  // time, figure out a way to avoid this duplication without causing
  // dependency headaches for CoreConfiguration.
  class TlsConfig : public ChannelCredsConfig {
   public:
    struct CertificateProviderInstance {
      std::string instance_name;
      std::string certificate_name;

      void PopulateFromProto(
          const envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance*
              proto,
          const CertificateProviderStoreInterface::PluginDefinitionMap&
              certificate_provider_definitions,
          ValidationErrors* errors) {
        instance_name = UpbStringToStdString(
            envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_instance_name(
                proto));
        if (certificate_provider_definitions.find(instance_name) ==
            certificate_provider_definitions.end()) {
          ValidationErrors::ScopedField field(errors, ".instance_name");
          errors->AddError(
              absl::StrCat("unrecognized certificate provider instance name: ",
                           instance_name));
        }
        certificate_name = UpbStringToStdString(
            envoy_extensions_transport_sockets_tls_v3_CommonTlsContext_CertificateProviderInstance_certificate_name(
                proto));
      }

      bool operator==(const CertificateProviderInstance& other) const {
        return instance_name == other.instance_name &&
               certificate_name == other.certificate_name;
      }

      std::string ToString() const {
        std::vector<std::string> parts;
        parts.push_back(absl::StrCat("instance_name=\"", instance_name, "\""));
        if (!certificate_name.empty()) {
          parts.push_back(
              absl::StrCat("certificate_name=\"", certificate_name, "\""));
        }
        return absl::StrCat("{", absl::StrJoin(parts, ","), "}");
      }
    };

    absl::string_view type() const override { return Type(); }

    absl::string_view proto_type() const override { return ProtoType(); }

    bool Equals(const ChannelCredsConfig& other) const override {
      auto& o = DownCast<const TlsConfig&>(other);
      return certificate_file_ == o.certificate_file_ &&
             private_key_file_ == o.private_key_file_ &&
             ca_certificate_file_ == o.ca_certificate_file_ &&
             refresh_interval_ == o.refresh_interval_ &&
             root_certificate_provider_ == o.root_certificate_provider_ &&
             identity_certificate_provider_ == o.identity_certificate_provider_;
    }

    std::string ToString() const override {
      std::vector<std::string> parts;
      if (!certificate_file_.empty()) {
        parts.push_back(absl::StrCat("certificate_file=", certificate_file_));
      }
      if (!private_key_file_.empty()) {
        parts.push_back(absl::StrCat("private_key_file=", private_key_file_));
      }
      if (!ca_certificate_file_.empty()) {
        parts.push_back(
            absl::StrCat("ca_certificate_file=", ca_certificate_file_));
      }
      if (refresh_interval_ != kDefaultRefreshInterval) {
        parts.push_back(
            absl::StrCat("refresh_interval=", refresh_interval_.ToString()));
      }
      if (!root_certificate_provider_.instance_name.empty()) {
        parts.push_back(absl::StrCat("root_cert_provider=",
                                     root_certificate_provider_.ToString()));
      }
      if (!identity_certificate_provider_.instance_name.empty()) {
        parts.push_back(
            absl::StrCat("identity_cert_provider=",
                         identity_certificate_provider_.ToString()));
      }
      return absl::StrCat("{", absl::StrJoin(parts, ","), "}");
    }

    const std::string& certificate_file() const { return certificate_file_; }
    const std::string& private_key_file() const { return private_key_file_; }
    const std::string& ca_certificate_file() const {
      return ca_certificate_file_;
    }
    Duration refresh_interval() const { return refresh_interval_; }

    const CertificateProviderInstance& root_certificate_provider() const {
      return root_certificate_provider_;
    }
    const CertificateProviderInstance& identity_certificate_provider() const {
      return identity_certificate_provider_;
    }

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

    static RefCountedPtr<const TlsConfig> ParseProto(
        absl::string_view serialized_proto,
        const CertificateProviderStoreInterface::PluginDefinitionMap&
            certificate_provider_definitions,
        ValidationErrors* errors) {
      upb::Arena arena;
      const auto* proto =
          envoy_extensions_grpc_service_channel_credentials_tls_v3_TlsCredentials_parse(
              serialized_proto.data(), serialized_proto.size(), arena.ptr());
      if (proto == nullptr) {
        errors->AddError("could not parse channel credentials config");
        return nullptr;
      }
      auto config = MakeRefCounted<TlsConfig>();
      // root_certificate_provider
      {
        ValidationErrors::ScopedField field(errors,
                                            ".root_certificate_provider");
        const auto* root_provider =
            envoy_extensions_grpc_service_channel_credentials_tls_v3_TlsCredentials_root_certificate_provider(
                proto);
        if (root_provider == nullptr) {
          errors->AddError("field not set");
        } else {
          config->root_certificate_provider_.PopulateFromProto(
              root_provider, certificate_provider_definitions, errors);
        }
      }
      // identity_certificate_provider
      {
        ValidationErrors::ScopedField field(errors,
                                            ".identity_certificate_provider");
        const auto* identity_provider =
            envoy_extensions_grpc_service_channel_credentials_tls_v3_TlsCredentials_identity_certificate_provider(
                proto);
        if (identity_provider != nullptr) {
          config->identity_certificate_provider_.PopulateFromProto(
              identity_provider, certificate_provider_definitions, errors);
        }
        return config;
      }
    }

   private:
    static constexpr Duration kDefaultRefreshInterval = Duration::Minutes(10);

    // Fields populated from xDS bootstrap file.
    std::string certificate_file_;
    std::string private_key_file_;
    std::string ca_certificate_file_;
    Duration refresh_interval_ = kDefaultRefreshInterval;

    // Fields populated from GrpcService proto credentials extension.
    CertificateProviderInstance root_certificate_provider_;
    CertificateProviderInstance identity_certificate_provider_;
  };

  static absl::string_view Type() { return "tls"; }

  static absl::string_view ProtoType() {
    return "envoy.extensions.grpc_service.channel_credentials.tls"
           ".v3.TlsCredentials";
  }
};

constexpr Duration TlsChannelCredsFactory::TlsConfig::kDefaultRefreshInterval;

class InsecureChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<const ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  absl::string_view proto_type() const override { return ProtoType(); }
  RefCountedPtr<const ChannelCredsConfig> ParseProto(
      absl::string_view /*serialized_config*/,
      const CertificateProviderStoreInterface::PluginDefinitionMap&
      /*certificate_provider_definitions*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<const ChannelCredsConfig> /*config*/,
      const CertificateProviderStoreInterface& /*certificate_provider_store*/)
      const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_insecure_credentials_create());
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    absl::string_view proto_type() const override { return ProtoType(); }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    std::string ToString() const override { return "{}"; }
  };

  static absl::string_view Type() { return "insecure"; }

  static absl::string_view ProtoType() {
    return "envoy.extensions.grpc_service.channel_credentials.insecure"
           ".v3.InsecureCredentials";
  }
};

class XdsChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return ""; }

  RefCountedPtr<const ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return nullptr;
  }

  absl::string_view proto_type() const override { return ProtoType(); }

  RefCountedPtr<const ChannelCredsConfig> ParseProto(
      absl::string_view serialized_config,
      const CertificateProviderStoreInterface::PluginDefinitionMap&
          certificate_provider_definitions,
      ValidationErrors* errors) const override {
    return Config::ParseProto(serialized_config,
                              certificate_provider_definitions, errors);
  }

  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<const ChannelCredsConfig> config,
      const CertificateProviderStoreInterface& certificate_provider_store)
      const override {
    auto fallback_creds =
        CoreConfiguration::Get().channel_creds_registry().CreateChannelCreds(
            DownCast<const Config&>(*config).fallback_credentials(),
            certificate_provider_store);
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_xds_credentials_create(fallback_creds.get()));
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return ""; }

    absl::string_view proto_type() const override { return ProtoType(); }

    bool Equals(const ChannelCredsConfig& other) const override {
      auto& o = DownCast<const Config&>(other);
      if (fallback_credentials_ == nullptr) {
        return o.fallback_credentials_ == nullptr;
      } else if (o.fallback_credentials_ == nullptr) {
        return false;
      }
      return *fallback_credentials_ == *o.fallback_credentials_;
    }

    std::string ToString() const override {
      return absl::StrCat(
          "{fallback_creds=",
          fallback_credentials_ == nullptr
              ? "<null>"
              : absl::StrCat("{type=", fallback_credentials_->type(),
                             ", config=", fallback_credentials_->ToString(),
                             "}"),
          "}");
    }

    RefCountedPtr<const ChannelCredsConfig> fallback_credentials() const {
      return fallback_credentials_;
    }

    static RefCountedPtr<const Config> ParseProto(
        absl::string_view serialized_proto,
        const CertificateProviderStoreInterface::PluginDefinitionMap&
            certificate_provider_definitions,
        ValidationErrors* errors) {
      upb::Arena arena;
      const auto* proto =
          envoy_extensions_grpc_service_channel_credentials_xds_v3_XdsCredentials_parse(
              serialized_proto.data(), serialized_proto.size(), arena.ptr());
      if (proto == nullptr) {
        errors->AddError("could not parse channel credentials config");
        return nullptr;
      }
      auto config = MakeRefCounted<Config>();
      ValidationErrors::ScopedField field(errors, ".fallback_credentials");
      const auto* fallback_creds_proto =
          envoy_extensions_grpc_service_channel_credentials_xds_v3_XdsCredentials_fallback_credentials(
              proto);
      if (fallback_creds_proto == nullptr) {
        errors->AddError("field not set");
      } else {
        absl::string_view type = absl::StripPrefix(
            UpbStringToAbsl(google_protobuf_Any_type_url(fallback_creds_proto)),
            "type.googleapis.com/");
        ValidationErrors::ScopedField field(errors, ".value");
        config->fallback_credentials_ =
            CoreConfiguration::Get().channel_creds_registry().ParseProto(
                type,
                UpbStringToAbsl(
                    google_protobuf_Any_value(fallback_creds_proto)),
                certificate_provider_definitions, errors);
      }
      return config;
    }

   private:
    RefCountedPtr<const ChannelCredsConfig> fallback_credentials_;
  };

  static absl::string_view ProtoType() {
    return "envoy.extensions.grpc_service.channel_credentials.xds.v3"
           ".XdsCredentials";
  }
};

class FakeChannelCredsFactory : public ChannelCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }
  RefCountedPtr<const ChannelCredsConfig> ParseConfig(
      const Json& /*config*/, const JsonArgs& /*args*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  absl::string_view proto_type() const override { return ""; }
  RefCountedPtr<const ChannelCredsConfig> ParseProto(
      absl::string_view /*serialized_config*/,
      const CertificateProviderStoreInterface::PluginDefinitionMap&
      /*certificate_provider_definitions*/,
      ValidationErrors* /*errors*/) const override {
    return MakeRefCounted<Config>();
  }
  RefCountedPtr<grpc_channel_credentials> CreateChannelCreds(
      RefCountedPtr<const ChannelCredsConfig> /*config*/,
      const CertificateProviderStoreInterface& /*certificate_provider_store*/)
      const override {
    return RefCountedPtr<grpc_channel_credentials>(
        grpc_fake_transport_security_credentials_create());
  }

 private:
  class Config : public ChannelCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }
    absl::string_view proto_type() const override { return ""; }
    bool Equals(const ChannelCredsConfig&) const override { return true; }
    std::string ToString() const override { return "{}"; }
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
      std::make_unique<XdsChannelCredsFactory>());
  builder->channel_creds_registry()->RegisterChannelCredsFactory(
      std::make_unique<FakeChannelCredsFactory>());
}

}  // namespace grpc_core
