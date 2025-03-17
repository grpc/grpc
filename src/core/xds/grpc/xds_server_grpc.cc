//
// Copyright 2019 gRPC authors.
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

#include "src/core/xds/grpc/xds_server_grpc.h"

#include <stdlib.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/json/json_writer.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kServerFeatureIgnoreResourceDeletion =
    "ignore_resource_deletion";

constexpr absl::string_view kServerFeatureFailOnDataErrors =
    "fail_on_data_errors";

constexpr absl::string_view kServerFeatureResourceTimerIsTransientFailure =
    "resource_timer_is_transient_error";

constexpr absl::string_view kServerFeatureTrustedXdsServer =
    "trusted_xds_server";

}  // namespace

bool GrpcXdsServer::IgnoreResourceDeletion() const {
  return server_features_.find(std::string(
             kServerFeatureIgnoreResourceDeletion)) != server_features_.end();
}

bool GrpcXdsServer::FailOnDataErrors() const {
  return server_features_.find(std::string(kServerFeatureFailOnDataErrors)) !=
         server_features_.end();
}

bool GrpcXdsServer::ResourceTimerIsTransientFailure() const {
  return server_features_.find(
             std::string(kServerFeatureResourceTimerIsTransientFailure)) !=
         server_features_.end();
}

bool GrpcXdsServer::TrustedXdsServer() const {
  return server_features_.find(std::string(kServerFeatureTrustedXdsServer)) !=
         server_features_.end();
}

bool GrpcXdsServer::Equals(const XdsServer& other) const {
  const auto& o = DownCast<const GrpcXdsServer&>(other);
  return (server_uri() == o.server_uri() &&
          channel_creds_config()->type() == o.channel_creds_config()->type() &&
          channel_creds_config()->Equals(*o.channel_creds_config()) &&
          server_features_ == o.server_features_);
}

std::string GrpcXdsServerTarget::Key() const { return JsonDump(ToJson()); }

std::string GrpcXdsServer::Key() const { return JsonDump(ToJson()); }

const JsonLoaderInterface* GrpcXdsServer::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<GrpcXdsServer>()
          .Finish();
  return loader;
}

namespace {

struct ChannelCreds {
  std::string type;
  Json::Object config;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<ChannelCreds>()
            .Field("type", &ChannelCreds::type)
            .OptionalField("config", &ChannelCreds::config)
            .Finish();
    return loader;
  }
};

}  // namespace

void GrpcXdsServer::JsonPostLoad(const Json& json, const JsonArgs& args,
  ValidationErrors* errors) {
RefCountedPtr<ChannelCredsConfig> channel_creds_config = nullptr;
// Parse "server_features".
{
ValidationErrors::ScopedField field(errors, ".server_features");
auto it = json.object().find("server_features");
if (it != json.object().end()) {
if (it->second.type() != Json::Type::kArray) {
errors->AddError("is not an array");
} else {
const Json::Array& array = it->second.array();
for (const Json& feature_json : array) {
if (feature_json.type() == Json::Type::kString &&
(feature_json.string() == kServerFeatureIgnoreResourceDeletion ||
feature_json.string() == kServerFeatureFailOnDataErrors ||
feature_json.string() ==
kServerFeatureResourceTimerIsTransientFailure ||
feature_json.string() == kServerFeatureTrustedXdsServer)) {
server_features_.insert(feature_json.string());
}
}
}
}
}
{
// Parse "channel_creds".
auto channel_creds_list = LoadJsonObjectField<std::vector<ChannelCreds>>(
json.object(), args, "channel_creds", errors);
if (channel_creds_list.has_value()) {
ValidationErrors::ScopedField field(errors, ".channel_creds");
for (size_t i = 0; i < channel_creds_list->size(); ++i) {
ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
auto& creds = (*channel_creds_list)[i];
// Select the first channel creds type that we support, but
// validate all entries.
if (CoreConfiguration::Get().channel_creds_registry().IsSupported(
creds.type)) {
ValidationErrors::ScopedField field(errors, ".config");
auto config =
CoreConfiguration::Get().channel_creds_registry().ParseConfig(
creds.type, Json::FromObject(creds.config), args, errors);
channel_creds_config = std::move(config);
}
}
if (channel_creds_config == nullptr) {
errors->AddError("no known creds type found");
}
}
}

// Parse "server_uri".
auto server_uri = LoadJsonObjectField<std::string>(
json.object(), args, "server_uri", errors);
if (server_uri.has_value()) {
server_target_ = std::make_shared<GrpcXdsServerTarget>(
server_uri.value(), channel_creds_config);
}
}

Json GrpcXdsServerTarget::ToJson() const {
  Json::Object channel_creds_json{
      {"type", Json::FromString(std::string(channel_creds_config_->type()))},
  };
  if (channel_creds_config() != nullptr) {
    channel_creds_json["config"] = channel_creds_config_->ToJson();
  }
  Json::Object json{
      {"server_uri", Json::FromString(server_uri_)},
      {"channel_creds",
       Json::FromArray({Json::FromObject(std::move(channel_creds_json))})},
  };
  return Json::FromObject(std::move(json));
}

Json GrpcXdsServer::ToJson() const {
  Json::Object channel_creds_json{
      {"type", Json::FromString(std::string(channel_creds_config()->type()))},
  };
  if (channel_creds_config() != nullptr) {
    channel_creds_json["config"] = channel_creds_config()->ToJson();
  }
  Json::Object json{
      {"server_uri", Json::FromString(server_uri())},
      {"channel_creds",
       Json::FromArray({Json::FromObject(std::move(channel_creds_json))})},
  };
  if (!server_features_.empty()) {
    Json::Array server_features_json;
    for (auto& feature : server_features_) {
      server_features_json.emplace_back(Json::FromString(feature));
    }
    json["server_features"] = Json::FromArray(std::move(server_features_json));
  }
  return Json::FromObject(std::move(json));
}

}  // namespace grpc_core
