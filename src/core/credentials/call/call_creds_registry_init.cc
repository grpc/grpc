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

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/call/call_creds_registry.h"
#include "src/core/credentials/call/jwt_token_file/jwt_token_file_call_credentials.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/validation_errors.h"

namespace grpc_core {

class JwtTokenFileCallCredsFactory : public CallCredsFactory<> {
 public:
  absl::string_view type() const override { return Type(); }

  RefCountedPtr<CallCredsConfig> ParseConfig(
      const Json& config, const JsonArgs& args,
      ValidationErrors* errors) const override {
    return LoadFromJson<RefCountedPtr<Config>>(config, args, errors);
  }

  RefCountedPtr<grpc_call_credentials> CreateCallCreds(
      RefCountedPtr<CallCredsConfig> base_config) const override {
    auto* config = DownCast<const Config*>(base_config.get());
    return MakeRefCounted<JwtTokenFileCallCredentials>(config->path());
  }

 private:
  class Config : public CallCredsConfig {
   public:
    absl::string_view type() const override { return Type(); }

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

    void JsonPostLoad(const Json& json, const JsonArgs& /*args*/,
                      ValidationErrors* errors) {
      if (json.object().find("jwt_token_file") != json.object().end() &&
          path_.empty()) {
        ValidationErrors::ScopedField field(errors, "jwt_token_file");
        errors->AddError("must be non-empty");
      }
    }

   private:
    std::string path_;
  };

  static absl::string_view Type() { return "jwt_token_file"; }
};

void RegisterDefaultCallCreds(CoreConfiguration::Builder* builder) {
  builder->call_creds_registry()->RegisterCallCredsFactory(
      std::make_unique<JwtTokenFileCallCredsFactory>());
}

}  // namespace grpc_core
