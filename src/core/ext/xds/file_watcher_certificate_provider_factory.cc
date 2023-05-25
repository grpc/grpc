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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/file_watcher_certificate_provider_factory.h"

#include <map>
#include <memory>
#include <utility>

#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/json/json_writer.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kFileWatcherPlugin = "file_watcher";

}  // namespace

//
// FileWatcherCertificateProviderFactory::Config
//

constexpr Duration
    FileWatcherCertificateProviderFactory::Config::kDefaultRefreshInterval;

absl::string_view FileWatcherCertificateProviderFactory::Config::name() const {
  return kFileWatcherPlugin;
}

Json FileWatcherCertificateProviderFactory::Config::ToJson() const {
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

std::string FileWatcherCertificateProviderFactory::Config::ToString() const {
  return JsonDump(ToJson());
}

const JsonLoaderInterface*
FileWatcherCertificateProviderFactory::Config::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<Config>()
          .OptionalField("certificate_file", &Config::certificate_file_)
          .OptionalField("private_key_file", &Config::private_key_file_)
          .OptionalField("ca_certificate_file", &Config::ca_certificate_file_)
          .OptionalField("refresh_interval", &Config::refresh_interval_)
          .Finish();
  return loader;
}

void FileWatcherCertificateProviderFactory::Config::JsonPostLoad(
    const Json& json, const JsonArgs& /*args*/, ValidationErrors* errors) {
  if ((json.object().find("certificate_file") == json.object().end()) !=
      (json.object().find("private_key_file") == json.object().end())) {
    errors->AddError(
        "fields \"certificate_file\" and \"private_key_file\" must be both set "
        "or both unset");
  }
  if ((json.object().find("certificate_file") == json.object().end()) &&
      (json.object().find("ca_certificate_file") == json.object().end())) {
    errors->AddError(
        "at least one of \"certificate_file\" and \"ca_certificate_file\" must "
        "be specified");
  }
}

//
// FileWatcherCertificateProviderFactory
//

absl::string_view FileWatcherCertificateProviderFactory::name() const {
  return kFileWatcherPlugin;
}

RefCountedPtr<CertificateProviderFactory::Config>
FileWatcherCertificateProviderFactory::CreateCertificateProviderConfig(
    const Json& config_json, const JsonArgs& args, ValidationErrors* errors) {
  return LoadFromJson<RefCountedPtr<Config>>(config_json, args, errors);
}

RefCountedPtr<grpc_tls_certificate_provider>
FileWatcherCertificateProviderFactory::CreateCertificateProvider(
    RefCountedPtr<CertificateProviderFactory::Config> config) {
  if (config->name() != name()) {
    gpr_log(GPR_ERROR, "Wrong config type Actual:%s vs Expected:%s",
            std::string(config->name()).c_str(), std::string(name()).c_str());
    return nullptr;
  }
  auto* file_watcher_config =
      static_cast<FileWatcherCertificateProviderFactory::Config*>(config.get());
  return MakeRefCounted<FileWatcherCertificateProvider>(
      file_watcher_config->private_key_file(),
      file_watcher_config->certificate_file(),
      file_watcher_config->ca_certificate_file(),
      file_watcher_config->refresh_interval().millis() / GPR_MS_PER_SEC);
}

void RegisterFileWatcherCertificateProvider(
    CoreConfiguration::Builder* builder) {
  builder->certificate_provider_registry()->RegisterCertificateProviderFactory(
      std::make_unique<FileWatcherCertificateProviderFactory>());
}

}  // namespace grpc_core
