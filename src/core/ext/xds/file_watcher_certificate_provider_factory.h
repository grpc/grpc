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

#ifndef GRPC_SRC_CORE_EXT_XDS_FILE_WATCHER_CERTIFICATE_PROVIDER_FACTORY_H
#define GRPC_SRC_CORE_EXT_XDS_FILE_WATCHER_CERTIFICATE_PROVIDER_FACTORY_H

#include <grpc/support/port_platform.h>

#include <string>

#include <grpc/grpc_security.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/security/certificate_provider/certificate_provider_factory.h"

namespace grpc_core {

class FileWatcherCertificateProviderFactory
    : public CertificateProviderFactory {
 public:
  class Config : public CertificateProviderFactory::Config {
   public:
    absl::string_view name() const override;

    std::string ToString() const override;

    Json ToJson() const;

    const std::string& certificate_file() const {
      return certificate_file_;
    }

    const std::string& private_key_file() const { return private_key_file_; }

    const std::string& ca_certificate_file() const {
      return ca_certificate_file_;
    }

    Duration refresh_interval() const { return refresh_interval_; }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs& args);
    void JsonPostLoad(const Json& source, const JsonArgs& args,
                      ValidationErrors* errors);

    bool Equals(const Config& other) const {
      return certificate_file_ == other.certificate_file_ &&
             private_key_file_ == other.private_key_file_ &&
             ca_certificate_file_ == other.ca_certificate_file_ &&
             refresh_interval_ == other.refresh_interval_;
    }

   private:
    static constexpr Duration kDefaultRefreshInterval = Duration::Minutes(10);

    std::string certificate_file_;
    std::string private_key_file_;
    std::string ca_certificate_file_;
    Duration refresh_interval_ = kDefaultRefreshInterval;
  };

  absl::string_view name() const override;

  RefCountedPtr<CertificateProviderFactory::Config>
  CreateCertificateProviderConfig(const Json& config_json, const JsonArgs& args,
                                  ValidationErrors* errors) override;

  RefCountedPtr<grpc_tls_certificate_provider> CreateCertificateProvider(
      RefCountedPtr<CertificateProviderFactory::Config> config) override;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_XDS_FILE_WATCHER_CERTIFICATE_PROVIDER_FACTORY_H
