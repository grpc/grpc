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
#include <grpcpp/impl/codegen/sync.h>
#include <grpcpp/support/config.h>

#include <functional>
#include <map>
#include <memory>
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

// Contains the verification-related information associated with a connection
// request. Users should not directly create or destroy this request object, but
// shall interact with it through CertificateVerifier's Verify() and Cancel().
// The object of this class should not outlive the underlying c object.
class TlsCustomVerificationCheckRequest {
 public:
  explicit TlsCustomVerificationCheckRequest(
      grpc_tls_custom_verification_check_request* request);
  ~TlsCustomVerificationCheckRequest() {}

  const std::string target_name() const;
  const std::string peer_cert() const;
  const std::string peer_cert_full_chain() const;
  const std::string common_name() const;
  std::vector<std::string> uri_names() const;
  std::vector<std::string> dns_names() const;
  std::vector<std::string> email_names() const;
  std::vector<std::string> ip_names() const;

  grpc_tls_custom_verification_check_request* c_request() { return c_request_; }

 private:
  grpc_tls_custom_verification_check_request* c_request_ = nullptr;
};

// The base class of all internal verifier implementations, and the ultimate
// class that all external verifiers will eventually be transformed into.
// - If you are a gRPC contributor and want to add an internal gRPC verifier,
//   simply add your new verifier by extending this base class, like
//   |HostNameCertificateVerifier|.
// - If you are a gRPC user and want to add a custom external verifier, create
//   your new verifier by extending |ExternalCertificateVerifier| as the base
//   class, and transform it to this class by using Create() method.
class CertificateVerifier {
 public:
  CertificateVerifier(grpc_tls_certificate_verifier* v) : verifier_(v) {}

  ~CertificateVerifier();

  // Verifies a connection request, based on the logic specified in an internal
  // verifier. The check on each internal verifier could be either synchronous
  // or asynchronous, and we will need to use return value to know.
  // Internally we invoke the verify() function of the c verifier object.
  // One typical usage of this function is to compose external verifiers with
  // internal verifiers. For example, users can compose their verifiers with a
  // |HostNameCertificateVerifier|. See "tls_test_utils.h" for some examples.
  //
  // request: the verification information associated with this request
  // callback: This will only take effect if the verifier is asynchronous.
  //           The function that gRPC will invoke when the verifier has already
  //           completed its asynchronous check. Callers can use this function
  //           to perform any additional checks. The input parameter of the
  //           std::function indicates the status of the verifier check.
  // sync_status: This will only be useful if the verifier is synchronous.
  //              The status of the verifier as it has already done it's
  //              synchronous check.
  // return: return true if executed synchronously, otherwise return false
  bool Verify(TlsCustomVerificationCheckRequest* request,
              std::function<void(grpc::Status)> callback,
              grpc::Status* sync_status);

  // Cancels a connection request. This is usually used to cleans up the
  // caller-specified resources when the verifier is still running but the whole
  // connection got cancelled.
  // This could happen when the verifier is doing some async operations, and the
  // whole handshaker object got destroyed because of connection time limit is
  // reached, or any other reasons. In such cases, function implementers might
  // want to be notified, and properly clean up some resources.
  //
  // request: the verification information associated with this request
  void Cancel(TlsCustomVerificationCheckRequest* request);

  // Gets the core verifier used internally.
  grpc_tls_certificate_verifier* c_verifier() { return verifier_; };

 private:
  static void AsyncCheckDone(
      grpc_tls_custom_verification_check_request* request, void* callback_arg,
      grpc_status_code status, const char* error_details);

  grpc_tls_certificate_verifier* verifier_ = nullptr;
  grpc::internal::Mutex mu_;
  std::map<grpc_tls_custom_verification_check_request*,
           std::function<void(grpc::Status)>>
      request_map_;
};

// The base class of all external, user-specified verifiers. Users should
// inherit this class if a custom verifier is expected to be used.
class ExternalCertificateVerifier {
 public:
  // A factory method for creating a |CertificateVerifier| from this class. All
  // the user-implemented verifiers should use this function to be converted to
  // verifiers compatible with |TlsCredentialsOptions|.
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

  // The verification logic that will be performed after the TLS handshake
  // completes. Implementers can choose to do their checks synchronously or
  // asynchronously, and return different values.
  //
  // request: the verification information associated with this request
  // callback: This should only be used if your check is done asynchronously.
  //           When the asynchronous work is done, invoke this callback function
  //           with the proper status, indicating the success or the failure of
  //           the check. The implementer MUST NOT invoke this |callback| in the
  //           same thread before Verify() returns, otherwise it can lead to
  //           deadlocks.
  // sync_status: This should only be used if your check is done synchronously.
  //              Modifies this value to indicate the success or the failure of
  //              the check.
  // return: return true if your check is done synchronously, otherwise return
  //         false
  virtual bool Verify(TlsCustomVerificationCheckRequest* request,
                      std::function<void(grpc::Status)> callback,
                      grpc::Status* sync_status) = 0;

  // The cancellation logic that will be performed when the verifier is still
  // running but the whole connection got cancelled.
  // This could happen when the verifier is doing some async operations, and the
  // whole handshaker object got destroyed because of connection time limit is
  // reached, or any other reasons. In such cases, function implementers might
  // want to be notified, and properly clean up some resources.
  //
  // request: the verification information associated with this request
  virtual void Cancel(TlsCustomVerificationCheckRequest* request) = 0;

 protected:
  ExternalCertificateVerifier();

  virtual ~ExternalCertificateVerifier();

 private:
  struct AsyncRequestState {
    AsyncRequestState(grpc_tls_on_custom_verification_check_done_cb cb,
                         void* arg,
                         grpc_tls_custom_verification_check_request* request)
        : callback(cb), callback_arg(arg), cpp_request(request) {}

    grpc_tls_on_custom_verification_check_done_cb callback;
    void* callback_arg;
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

  // TODO(yihuazhang): after the insecure build is removed, make this an object
  // member instead of a pointer.
  grpc_tls_certificate_verifier_external* base_ = nullptr;
  grpc::internal::Mutex mu_;
  std::map<grpc_tls_custom_verification_check_request*, AsyncRequestState>
      request_map_;
};

class HostNameCertificateVerifier : public CertificateVerifier {
 public:
  HostNameCertificateVerifier();
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_TLS_CERTIFICATE_VERIFIER_H
