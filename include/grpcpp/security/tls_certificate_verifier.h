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

#ifndef GRPCPP_SECURITY_TLS_CERTIFICATE_VERIFIER_H
#define GRPCPP_SECURITY_TLS_CERTIFICATE_VERIFIER_H

#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/grpc_library.h>
#include <grpcpp/support/config.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// TODO(yihuazhang): remove the forward declaration here and include
// <grpc/grpc_security.h> directly once the insecure builds are cleaned up.
typedef struct grpc_tls_custom_verification_check_request
    grpc_tls_custom_verification_check_request;
typedef struct grpc_tls_certificate_verifier grpc_tls_certificate_verifier;
typedef struct grpc_tls_certificate_verifier_external
    grpc_tls_certificate_verifier_external;
typedef void (*grpc_tls_on_custom_verification_check_done_cb)(
    grpc_tls_custom_verification_check_request* request, void* callback_arg,
    grpc_status_code status, const char* error_details);
extern "C" grpc_tls_certificate_verifier*
grpc_tls_certificate_verifier_external_create(
    grpc_tls_certificate_verifier_external* external_verifier);

namespace grpc {
namespace experimental {

// Users should not directly create or destroy the request object. We will
// handle it in CertificateVerifier or TlsCredentialsOptions.
class TlsCustomVerificationCheckRequest {
 public:
  explicit TlsCustomVerificationCheckRequest(
      grpc_tls_custom_verification_check_request* request);
  ~TlsCustomVerificationCheckRequest() {}

  // Getters that are exposed publicly.
  const std::string& target_name() const { return target_name_; }
  const std::string& peer_cert() const { return peer_cert_; }
  const std::string& peer_cert_full_chain() const {
    return peer_cert_full_chain_;
  }
  const std::string& common_name() const { return common_name_; }
  std::vector<std::string> uri_names() const { return uri_names_; }
  std::vector<std::string> dns_names() const { return dns_names_; }
  std::vector<std::string> email_names() const { return email_names_; }
  std::vector<std::string> ip_names() const { return ip_names_; }

  grpc_tls_custom_verification_check_request* c_request() { return c_request_; }

 private:
  grpc_tls_custom_verification_check_request* c_request_ = nullptr;
  std::string target_name_;
  std::string peer_cert_;
  std::string peer_cert_full_chain_;
  std::string common_name_;
  std::vector<std::string> uri_names_;
  std::vector<std::string> dns_names_;
  std::vector<std::string> email_names_;
  std::vector<std::string> ip_names_;
};

class CertificateVerifier {
 public:
  CertificateVerifier(grpc_tls_certificate_verifier* v) : verifier_(v) {}

  ~CertificateVerifier();

  bool Verify(TlsCustomVerificationCheckRequest* request,
              std::function<void(grpc::Status)> callback,
              grpc::Status* sync_status);

  void Cancel(TlsCustomVerificationCheckRequest* request);

  // gets the core verifier used internally.
  grpc_tls_certificate_verifier* c_verifier() { return verifier_; };

 private:
  static void AsyncCheckDone(
      grpc_tls_custom_verification_check_request* request, void* callback_arg,
      grpc_status_code status, const char* error_details);

  grpc_tls_certificate_verifier* verifier_ = nullptr;
  std::mutex mu_;
  std::map<grpc_tls_custom_verification_check_request*,
           std::function<void(grpc::Status)>>
      request_map_;
};

class ExternalCertificateVerifier {
 public:
  virtual bool Verify(TlsCustomVerificationCheckRequest* request,
                      std::function<void(grpc::Status)> callback,
                      grpc::Status* sync_status) = 0;

  virtual void Cancel(TlsCustomVerificationCheckRequest* request) = 0;

  // Subclass is created here, but it will be destroyed when the core
  // ExternalCertificateVerifier is destroyed. The reason is we need to access
  // Subclass when invoking the user-specified callbacks, and hence we need to
  // bind its lifetime to the core objects.
  template <typename Subclass, typename... Args>
  static std::shared_ptr<CertificateVerifier> Create(Args&&... args) {
    auto* external_verifier = new Subclass(std::forward<Args>(args)...);
    return std::make_shared<CertificateVerifier>(
        grpc_tls_certificate_verifier_external_create(
            external_verifier->base_));
  }

 protected:
  ExternalCertificateVerifier();

  virtual ~ExternalCertificateVerifier();

 private:
  struct AsyncCallbackProfile {
    AsyncCallbackProfile(grpc_tls_on_custom_verification_check_done_cb cb,
                         void* arg,
                         grpc_tls_custom_verification_check_request* request)
        : callback(cb), callback_arg(arg), cpp_request(request) {}

    grpc_tls_on_custom_verification_check_done_cb callback = nullptr;
    void* callback_arg = nullptr;
    TlsCustomVerificationCheckRequest cpp_request;
  };

  static int VerifyInCoreExternalVerifier(
      void* user_data, grpc_tls_custom_verification_check_request* request,
      grpc_tls_on_custom_verification_check_done_cb callback,
      void* callback_arg, grpc_status_code* sync_status,
      char** sync_error_details);

  static void CancelInCoreExternalVerifier(
      void* user_data, grpc_tls_custom_verification_check_request* request);

  static void DestructInCoreExternalVerifier(void* user_data);

  // We have to use a pointer here, because
  // grpc_tls_certificate_verifier_external is an incomplete type. Question:
  // what's the prgress of insecure build clean-up? Or do we have any other ways
  // to bypass this?
  grpc_tls_certificate_verifier_external* base_ = nullptr;
  std::mutex mu_;
  std::map<grpc_tls_custom_verification_check_request*, AsyncCallbackProfile>
      request_map_;
};

class HostNameCertificateVerifier : public CertificateVerifier {
 public:
  HostNameCertificateVerifier();
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_TLS_CERTIFICATE_VERIFIER_H
