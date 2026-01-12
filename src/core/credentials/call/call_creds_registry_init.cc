//
// Copyright 2025 gRPC authors.
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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/json.h>

#include <memory>
#include <string>

#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.upb.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/call/call_creds_registry.h"
#include "src/core/credentials/call/jwt_token_file/jwt_token_file_call_credentials.h"
#include "src/core/credentials/call/oauth2/oauth2_credentials.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class JwtTokenFileCallCredsFactory : public CallCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }

  RefCountedPtr<const CallCredsConfig> ParseConfig(
      const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const override {
    return LoadFromJson<RefCountedPtr<Config>>(config, args, errors);
  }

  absl::string_view proto_type() const override { return ""; }

  RefCountedPtr<const CallCredsConfig> ParseProto(
      absl::string_view /*serialized_proto*/,
      ValidationErrors* /*errors*/) const override {
    return nullptr;
  }

  RefCountedPtr<grpc_call_credentials> CreateCallCreds(
      RefCountedPtr<const CallCredsConfig> base_config) const override {
    auto* config = DownCast<const Config*>(base_config.get());
    return MakeRefCounted<JwtTokenFileCallCredentials>(config->path());
  }

 private:
  class Config : public CallCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }

    absl::string_view proto_type() const override { return ""; }

    bool Equals(const CallCredsConfig& other) const override {
      auto& o = DownCast<const Config&>(other);
      return path_ == o.path_;
    }

    std::string ToString() const override {
      return absl::StrCat("{path=\"", path_, "\"}");
    }

    const std::string& path() const { return path_; }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<Config>()
                                      .Field("jwt_token_file", &Config::path_)
                                      .Finish();
      return loader;
    }

   private:
    std::string path_;
  };

  static absl::string_view Type() { return "jwt_token_file"; }
};

class AccessTokenCallCredsFactory : public CallCredsFactory<> {
 public:
  absl::string_view type() const override { return ""; }

  RefCountedPtr<const CallCredsConfig> ParseConfig(
      const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const override {
    return nullptr;
  }

  absl::string_view proto_type() const override { return ProtoType(); }

  RefCountedPtr<const CallCredsConfig> ParseProto(
      absl::string_view serialized_proto,
      ValidationErrors* errors) const override {
    upb::Arena arena;
    const auto* proto =
        envoy_extensions_grpc_service_call_credentials_access_token_v3_AccessTokenCredentials_parse(
            serialized_proto.data(), serialized_proto.size(), arena.ptr());
    if (proto == nullptr) {
      errors->AddError("could not parse call credentials config");
      return nullptr;
    }
    absl::string_view token = UpbStringToAbsl(
        envoy_extensions_grpc_service_call_credentials_access_token_v3_AccessTokenCredentials_token(
            proto));
    if (token.empty()) {
      ValidationErrors::ScopedField field(errors, ".token");
      errors->AddError("field not present");
    }
    return MakeRefCounted<Config>(token);
  }

  RefCountedPtr<grpc_call_credentials> CreateCallCreds(
      RefCountedPtr<const CallCredsConfig> base_config) const override {
    auto* config = DownCast<const Config*>(base_config.get());
    return MakeRefCounted<grpc_access_token_credentials>(
        config->token().c_str());
  }

 private:
  class Config : public CallCredsConfig {
   public:
    explicit Config(absl::string_view token) : token_(token) {}

    absl::string_view type() const override { return ""; }

    absl::string_view proto_type() const override { return ProtoType(); }

    bool Equals(const CallCredsConfig& other) const override {
      auto& o = DownCast<const Config&>(other);
      return token_ == o.token_;
    }

    std::string ToString() const override {
      return absl::StrCat("{token=\"", token_, "\"}");
    }

    const std::string& token() const { return token_; }

   private:
    std::string token_;
  };

  static absl::string_view ProtoType() {
    return "envoy.extensions.grpc_service.call_credentials.access_token"
           ".v3.AccessTokenCredentials";
  }
};

void RegisterDefaultCallCreds(CoreConfiguration::Builder* builder) {
  builder->call_creds_registry()->RegisterCallCredsFactory(
      std::make_unique<JwtTokenFileCallCredsFactory>());
  builder->call_creds_registry()->RegisterCallCredsFactory(
      std::make_unique<AccessTokenCallCredsFactory>());
}

}  // namespace grpc_core
