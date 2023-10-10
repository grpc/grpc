//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CRL_PROVIDER_H
#define GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CRL_PROVIDER_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include <openssl/crypto.h>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_crl_provider.h>

#include "src/core/lib/event_engine/default_event_engine.h"

namespace grpc_core {
namespace experimental {

class CrlImpl : public Crl {
 public:
  static absl::StatusOr<std::unique_ptr<CrlImpl>> Create(X509_CRL* crl);
  // Takes ownership of the X509_CRL pointer.
  CrlImpl(X509_CRL* crl, absl::string_view issuer)
      : crl_(crl), issuer_(issuer) {}
  // Makes a copy of the X509_CRL
  CrlImpl(const CrlImpl& other);
  ~CrlImpl() override;
  // Returns a string view representation of the issuer pulled from the CRL.
  absl::string_view Issuer() override { return issuer_; }
  // The caller should not take ownership of the returned pointer.
  X509_CRL* crl() const { return crl_; }

 private:
  X509_CRL* crl_;
  const std::string issuer_;
};

class CertificateInfoImpl : public CertificateInfo {
 public:
  explicit CertificateInfoImpl(absl::string_view issuer) : issuer_(issuer) {}
  // Returns a string representation of the issuer pulled from the
  // certificate.
  absl::string_view Issuer() const override { return issuer_; }

 private:
  const std::string issuer_;
};

// Defining this here lets us hide implementation details (and includes) from
// the header in include
class DirectoryReloaderCrlProviderImpl
    : public DirectoryReloaderCrlProvider,
      public std::enable_shared_from_this<DirectoryReloaderCrlProviderImpl> {
 public:
  DirectoryReloaderCrlProviderImpl(::absl::string_view directory,
                                   ::std::chrono::seconds duration,
                                   ::std::function<void(absl::Status)> callback)
      : crl_directory_(directory),
        refresh_duration_(duration),
        reload_error_callback_(callback),
        event_engine_(
            grpc_event_engine::experimental::GetDefaultEventEngine()) {}
  ~DirectoryReloaderCrlProviderImpl() override;
  std::shared_ptr<Crl> GetCrl(const CertificateInfo& certificate_info) override;
  void ScheduleReload();
  bool OnNextUpdateTimer();

  ::absl::Status Update();
  ::absl::flat_hash_map<::std::string, ::std::shared_ptr<Crl>> crls_;
  ::std::string crl_directory_;
  ::absl::Mutex mu_;
  ::std::chrono::seconds refresh_duration_;
  ::std::function<void(::absl::Status)> reload_error_callback_;
  gpr_event shutdown_event_;
  absl::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
      refresh_handle_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CRL_PROVIDER_H