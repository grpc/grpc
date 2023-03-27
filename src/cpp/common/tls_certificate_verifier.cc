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

#include <stddef.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <grpc/grpc_security.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/security/tls_certificate_verifier.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/string_ref.h>

namespace grpc {
namespace experimental {

TlsCustomVerificationCheckRequest::TlsCustomVerificationCheckRequest(
    grpc_tls_custom_verification_check_request* request)
    : c_request_(request) {
  GPR_ASSERT(c_request_ != nullptr);
}

grpc::string_ref TlsCustomVerificationCheckRequest::target_name() const {
  return c_request_->target_name != nullptr ? c_request_->target_name : "";
}

grpc::string_ref TlsCustomVerificationCheckRequest::peer_cert() const {
  return c_request_->peer_info.peer_cert != nullptr
             ? c_request_->peer_info.peer_cert
             : "";
}

grpc::string_ref TlsCustomVerificationCheckRequest::peer_cert_full_chain()
    const {
  return c_request_->peer_info.peer_cert_full_chain != nullptr
             ? c_request_->peer_info.peer_cert_full_chain
             : "";
}

grpc::string_ref TlsCustomVerificationCheckRequest::common_name() const {
  return c_request_->peer_info.common_name != nullptr
             ? c_request_->peer_info.common_name
             : "";
}

grpc::string_ref TlsCustomVerificationCheckRequest::verified_root_cert_subject()
    const {
  return c_request_->peer_info.verified_root_cert_subject != nullptr
             ? c_request_->peer_info.verified_root_cert_subject
             : "";
}

std::vector<grpc::string_ref> TlsCustomVerificationCheckRequest::uri_names()
    const {
  std::vector<grpc::string_ref> uri_names;
  for (size_t i = 0; i < c_request_->peer_info.san_names.uri_names_size; ++i) {
    uri_names.emplace_back(c_request_->peer_info.san_names.uri_names[i]);
  }
  return uri_names;
}

std::vector<grpc::string_ref> TlsCustomVerificationCheckRequest::dns_names()
    const {
  std::vector<grpc::string_ref> dns_names;
  for (size_t i = 0; i < c_request_->peer_info.san_names.dns_names_size; ++i) {
    dns_names.emplace_back(c_request_->peer_info.san_names.dns_names[i]);
  }
  return dns_names;
}

std::vector<grpc::string_ref> TlsCustomVerificationCheckRequest::email_names()
    const {
  std::vector<grpc::string_ref> email_names;
  for (size_t i = 0; i < c_request_->peer_info.san_names.email_names_size;
       ++i) {
    email_names.emplace_back(c_request_->peer_info.san_names.email_names[i]);
  }
  return email_names;
}

std::vector<grpc::string_ref> TlsCustomVerificationCheckRequest::ip_names()
    const {
  std::vector<grpc::string_ref> ip_names;
  for (size_t i = 0; i < c_request_->peer_info.san_names.ip_names_size; ++i) {
    ip_names.emplace_back(c_request_->peer_info.san_names.ip_names[i]);
  }
  return ip_names;
}

CertificateVerifier::CertificateVerifier(grpc_tls_certificate_verifier* v)
    : verifier_(v) {}

CertificateVerifier::~CertificateVerifier() {
  grpc_tls_certificate_verifier_release(verifier_);
}

bool CertificateVerifier::Verify(TlsCustomVerificationCheckRequest* request,
                                 std::function<void(grpc::Status)> callback,
                                 grpc::Status* sync_status) {
  GPR_ASSERT(request != nullptr);
  GPR_ASSERT(request->c_request() != nullptr);
  {
    internal::MutexLock lock(&mu_);
    request_map_.emplace(request->c_request(), std::move(callback));
  }
  grpc_status_code status_code = GRPC_STATUS_OK;
  char* error_details = nullptr;
  bool is_done = grpc_tls_certificate_verifier_verify(
      verifier_, request->c_request(), &AsyncCheckDone, this, &status_code,
      &error_details);
  if (is_done) {
    if (status_code != GRPC_STATUS_OK) {
      *sync_status = grpc::Status(static_cast<grpc::StatusCode>(status_code),
                                  error_details);
    }
    internal::MutexLock lock(&mu_);
    request_map_.erase(request->c_request());
  }
  gpr_free(error_details);
  return is_done;
}

void CertificateVerifier::Cancel(TlsCustomVerificationCheckRequest* request) {
  GPR_ASSERT(request != nullptr);
  GPR_ASSERT(request->c_request() != nullptr);
  grpc_tls_certificate_verifier_cancel(verifier_, request->c_request());
}

void CertificateVerifier::AsyncCheckDone(
    grpc_tls_custom_verification_check_request* request, void* callback_arg,
    grpc_status_code status, const char* error_details) {
  auto* self = static_cast<CertificateVerifier*>(callback_arg);
  std::function<void(grpc::Status)> callback;
  {
    internal::MutexLock lock(&self->mu_);
    auto it = self->request_map_.find(request);
    if (it != self->request_map_.end()) {
      callback = std::move(it->second);
      self->request_map_.erase(it);
    }
  }
  if (callback != nullptr) {
    grpc::Status return_status;
    if (status != GRPC_STATUS_OK) {
      return_status =
          grpc::Status(static_cast<grpc::StatusCode>(status), error_details);
    }
    callback(return_status);
  }
}

ExternalCertificateVerifier::ExternalCertificateVerifier() {
  base_ = new grpc_tls_certificate_verifier_external();
  base_->user_data = this;
  base_->verify = VerifyInCoreExternalVerifier;
  base_->cancel = CancelInCoreExternalVerifier;
  base_->destruct = DestructInCoreExternalVerifier;
}

ExternalCertificateVerifier::~ExternalCertificateVerifier() { delete base_; }

int ExternalCertificateVerifier::VerifyInCoreExternalVerifier(
    void* user_data, grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback, void* callback_arg,
    grpc_status_code* sync_status, char** sync_error_details) {
  auto* self = static_cast<ExternalCertificateVerifier*>(user_data);
  TlsCustomVerificationCheckRequest* cpp_request = nullptr;
  {
    internal::MutexLock lock(&self->mu_);
    auto pair = self->request_map_.emplace(
        request, AsyncRequestState(callback, callback_arg, request));
    GPR_ASSERT(pair.second);
    cpp_request = &pair.first->second.cpp_request;
  }
  grpc::Status sync_current_verifier_status;
  bool is_done = self->Verify(
      cpp_request,
      [self, request](grpc::Status status) {
        grpc_tls_on_custom_verification_check_done_cb callback = nullptr;
        void* callback_arg = nullptr;
        {
          internal::MutexLock lock(&self->mu_);
          auto it = self->request_map_.find(request);
          if (it != self->request_map_.end()) {
            callback = it->second.callback;
            callback_arg = it->second.callback_arg;
            self->request_map_.erase(it);
          }
        }
        if (callback != nullptr) {
          callback(request, callback_arg,
                   static_cast<grpc_status_code>(status.error_code()),
                   status.error_message().c_str());
        }
      },
      &sync_current_verifier_status);
  if (is_done) {
    if (!sync_current_verifier_status.ok()) {
      *sync_status = static_cast<grpc_status_code>(
          sync_current_verifier_status.error_code());
      *sync_error_details =
          gpr_strdup(sync_current_verifier_status.error_message().c_str());
    }
    internal::MutexLock lock(&self->mu_);
    self->request_map_.erase(request);
  }
  return is_done;
}

void ExternalCertificateVerifier::CancelInCoreExternalVerifier(
    void* user_data, grpc_tls_custom_verification_check_request* request) {
  auto* self = static_cast<ExternalCertificateVerifier*>(user_data);
  TlsCustomVerificationCheckRequest* cpp_request = nullptr;
  {
    internal::MutexLock lock(&self->mu_);
    auto it = self->request_map_.find(request);
    if (it != self->request_map_.end()) {
      cpp_request = &it->second.cpp_request;
    }
  }
  if (cpp_request != nullptr) {
    self->Cancel(cpp_request);
  }
}

void ExternalCertificateVerifier::DestructInCoreExternalVerifier(
    void* user_data) {
  auto* self = static_cast<ExternalCertificateVerifier*>(user_data);
  delete self;
}

NoOpCertificateVerifier::NoOpCertificateVerifier()
    : CertificateVerifier(grpc_tls_certificate_verifier_no_op_create()) {}

HostNameCertificateVerifier::HostNameCertificateVerifier()
    : CertificateVerifier(grpc_tls_certificate_verifier_host_name_create()) {}

}  // namespace experimental
}  // namespace grpc
