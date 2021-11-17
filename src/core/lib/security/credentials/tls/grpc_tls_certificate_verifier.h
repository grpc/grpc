//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_VERIFIER_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_VERIFIER_H

#include <grpc/support/port_platform.h>

#include <string.h>

#include "absl/status/status.h"

#include <grpc/grpc_security.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

// An abstraction of the verifier that all verifier subclasses should extend.
struct grpc_tls_certificate_verifier
    : public grpc_core::RefCounted<grpc_tls_certificate_verifier> {
 public:
  grpc_tls_certificate_verifier() = default;

  ~grpc_tls_certificate_verifier() override = default;
  // Verifies the specific request. It can be processed in sync or async mode.
  // If the caller want it to be processed asynchronously, return false
  // immediately, and at the end of the async operation, invoke the callback
  // with the verification results stored in absl::Status. Otherwise, populate
  // the verification results in |sync_status| and return true. The caller is
  // expected to populate verification results by setting request.
  virtual bool Verify(grpc_tls_custom_verification_check_request* request,
                      std::function<void(absl::Status)> callback,
                      absl::Status* sync_status) = 0;
  // Operations that will be performed when a request is cancelled.
  // This is only needed when in async mode.
  virtual void Cancel(grpc_tls_custom_verification_check_request* request) = 0;
};

namespace grpc_core {

// A verifier that will transform grpc_tls_certificate_verifier_external to a
// verifier that extends grpc_tls_certificate_verifier.
class ExternalCertificateVerifier : public grpc_tls_certificate_verifier {
 public:
  explicit ExternalCertificateVerifier(
      grpc_tls_certificate_verifier_external* external_verifier)
      : external_verifier_(external_verifier) {}

  ~ExternalCertificateVerifier() override {
    if (external_verifier_->destruct != nullptr) {
      external_verifier_->destruct(external_verifier_->user_data);
    }
  }

  bool Verify(grpc_tls_custom_verification_check_request* request,
              std::function<void(absl::Status)> callback,
              absl::Status* sync_status) override;

  void Cancel(grpc_tls_custom_verification_check_request* request) override {
    external_verifier_->cancel(external_verifier_->user_data, request);
  }

 private:
  grpc_tls_certificate_verifier_external* external_verifier_;

  static void OnVerifyDone(grpc_tls_custom_verification_check_request* request,
                           void* callback_arg, grpc_status_code status,
                           const char* error_details);
  // Guards members below.
  Mutex mu_;
  // stores each check request and its corresponding callback function.
  std::map<grpc_tls_custom_verification_check_request*,
           std::function<void(absl::Status)>>
      request_map_ ABSL_GUARDED_BY(mu_);
};

// An internal verifier that will perform hostname verification check.
class HostNameCertificateVerifier : public grpc_tls_certificate_verifier {
 public:
  bool Verify(grpc_tls_custom_verification_check_request* request,
              std::function<void(absl::Status)> callback,
              absl::Status* sync_status) override;
  void Cancel(grpc_tls_custom_verification_check_request*) override {}
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_CERTIFICATE_VERIFIER_H
