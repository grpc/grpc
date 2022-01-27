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

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/lib/json/json_util.h"

namespace grpc_core {

namespace {

const char* kFileWatcherPlugin = "file_watcher";

}  // namespace

//
// FileWatcherCertificateProviderFactory::Config
//

const char* FileWatcherCertificateProviderFactory::Config::name() const {
  return kFileWatcherPlugin;
}

std::string FileWatcherCertificateProviderFactory::Config::ToString() const {
  std::vector<std::string> parts;
  parts.push_back("{");
  if (!identity_cert_file_.empty()) {
    parts.push_back(
        absl::StrFormat("certificate_file=\"%s\", ", identity_cert_file_));
  }
  if (!identity_cert_file_.empty()) {
    parts.push_back(
        absl::StrFormat("private_key_file=\"%s\", ", private_key_file_));
  }
  if (!identity_cert_file_.empty()) {
    parts.push_back(
        absl::StrFormat("ca_certificate_file=\"%s\", ", root_cert_file_));
  }
  parts.push_back(
      absl::StrFormat("refresh_interval=%ldms}", refresh_interval_ms_));
  return absl::StrJoin(parts, "");
}

RefCountedPtr<FileWatcherCertificateProviderFactory::Config>
FileWatcherCertificateProviderFactory::Config::Parse(const Json& config_json,
                                                     grpc_error_handle* error) {
  auto config = MakeRefCounted<FileWatcherCertificateProviderFactory::Config>();
  if (config_json.type() != Json::Type::OBJECT) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "error:config type should be OBJECT.");
    return nullptr;
  }
  std::vector<grpc_error_handle> error_list;
  ParseJsonObjectField(config_json.object_value(), "certificate_file",
                       &config->identity_cert_file_, &error_list, false);
  ParseJsonObjectField(config_json.object_value(), "private_key_file",
                       &config->private_key_file_, &error_list, false);
  if (config->identity_cert_file_.empty() !=
      config->private_key_file_.empty()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "fields \"certificate_file\" and \"private_key_file\" must be both set "
        "or both unset."));
  }
  ParseJsonObjectField(config_json.object_value(), "ca_certificate_file",
                       &config->root_cert_file_, &error_list, false);
  if (config->identity_cert_file_.empty() && config->root_cert_file_.empty()) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "At least one of \"certificate_file\" and \"ca_certificate_file\" must "
        "be specified."));
  }
  if (!ParseJsonObjectFieldAsDuration(
          config_json.object_value(), "refresh_interval",
          &config->refresh_interval_ms_, &error_list, false)) {
    config->refresh_interval_ms_ = 10 * 60 * 1000;  // 10 minutes default
  }
  if (!error_list.empty()) {
    *error = GRPC_ERROR_CREATE_FROM_VECTOR(
        "Error parsing file watcher certificate provider config", &error_list);
    return nullptr;
  }
  return config;
}

//
// FileWatcherCertificateProviderFactory
//

const char* FileWatcherCertificateProviderFactory::name() const {
  return kFileWatcherPlugin;
}

RefCountedPtr<CertificateProviderFactory::Config>
FileWatcherCertificateProviderFactory::CreateCertificateProviderConfig(
    const Json& config_json, grpc_error_handle* error) {
  return FileWatcherCertificateProviderFactory::Config::Parse(config_json,
                                                              error);
}

RefCountedPtr<grpc_tls_certificate_provider>
FileWatcherCertificateProviderFactory::CreateCertificateProvider(
    RefCountedPtr<CertificateProviderFactory::Config> config) {
  if (config->name() != name()) {
    gpr_log(GPR_ERROR, "Wrong config type Actual:%s vs Expected:%s",
            config->name(), name());
    return nullptr;
  }
  auto* file_watcher_config =
      static_cast<FileWatcherCertificateProviderFactory::Config*>(config.get());
  return MakeRefCounted<FileWatcherCertificateProvider>(
      file_watcher_config->private_key_file(),
      file_watcher_config->identity_cert_file(),
      file_watcher_config->root_cert_file(),
      file_watcher_config->refresh_interval_ms() / GPR_MS_PER_SEC);
}

void FileWatcherCertificateProviderInit() {
  CertificateProviderRegistry::RegisterCertificateProviderFactory(
      absl::make_unique<FileWatcherCertificateProviderFactory>());
}

void FileWatcherCertificateProviderShutdown() {}

}  // namespace grpc_core
