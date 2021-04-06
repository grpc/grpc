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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/stat.h"
#include "src/core/lib/security/credentials/tls/tls_utils.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

void CertificateVerificationRequest::CertificateVerificationRequestInit(
    grpc_tls_custom_verification_check_request* request) {
  GPR_ASSERT(request != nullptr);
  request->target_name = nullptr;
  request->peer_info.common_name = nullptr;
  request->peer_info.san_names.uri_names = nullptr;
  request->peer_info.san_names.uri_names_size = 0;
  request->peer_info.san_names.ip_names = nullptr;
  request->peer_info.san_names.ip_names_size = 0;
  request->peer_info.san_names.dns_names = nullptr;
  request->peer_info.san_names.dns_names_size = 0;
  request->peer_info.peer_cert = nullptr;
  request->peer_info.peer_cert_full_chain = nullptr;
  request->status = GRPC_STATUS_CANCELLED;
  request->error_details = nullptr;
}

void CertificateVerificationRequest::CertificateVerificationRequestDestroy(
    grpc_tls_custom_verification_check_request* request) {
  GPR_ASSERT(request != nullptr);
  if (request->target_name != nullptr) {
    gpr_free(const_cast<char*>(request->target_name));
  }
  if (request->peer_info.common_name != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.common_name));
  }
  if (request->peer_info.san_names.uri_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.uri_names_size; ++i) {
      delete[] request->peer_info.san_names.uri_names[i];
    }
    delete[] request->peer_info.san_names.uri_names;
  }
  if (request->peer_info.san_names.ip_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.ip_names_size; ++i) {
      delete[] request->peer_info.san_names.ip_names[i];
    }
    delete[] request->peer_info.san_names.ip_names;
  }
  if (request->peer_info.san_names.dns_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.dns_names_size; ++i) {
      delete[] request->peer_info.san_names.dns_names[i];
    }
    delete[] request->peer_info.san_names.dns_names;
  }
  if (request->peer_info.peer_cert != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.peer_cert));
  }
  if (request->peer_info.peer_cert_full_chain != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.peer_cert_full_chain));
  }
  if (request->error_details != nullptr) {
    gpr_free(const_cast<char*>(request->error_details));
  }
}

bool ExternalCertificateVerifier::Verify(
    CertificateVerificationRequest* internal_request,
    std::function<void()> callback) {
  {
    grpc_core::MutexLock lock(&mu_);
    request_map_.emplace(internal_request, std::move(callback));
  }
  // Invoke the caller-specified verification logic embedded in
  // external_verifier_.
  bool is_async = external_verifier_->verify(external_verifier_->user_data,
                                             &internal_request->request,
                                             &OnVerifyDone, this);
  if (!is_async) {
    grpc_core::MutexLock lock(&mu_);
    request_map_.erase(internal_request);
  }
  return is_async;
}

void ExternalCertificateVerifier::OnVerifyDone(
    grpc_tls_custom_verification_check_request* request, void* user_data) {
  grpc_core::ExecCtx exec_ctx;
  auto* self = static_cast<ExternalCertificateVerifier*>(user_data);
  CertificateVerificationRequest* internal_request =
      reinterpret_cast<CertificateVerificationRequest*>(request);
  grpc_core::MutexLock lock(&self->mu_);
  auto it = self->request_map_.find(internal_request);
  if (it != self->request_map_.end()) {
    std::function<void()> callback = std::move(it->second);
    callback();
    self->request_map_.erase(it->first);
  }
}

namespace internal {

static bool VerifyIdentityAsHostname(absl::string_view identity_name,
                                     absl::string_view target_name) {
  // We are using the target name sent from the client as a matcher to match
  // against identity name on the peer cert.
  return grpc_core::VerifySubjectAlternativeName(identity_name,
                                                 std::string(target_name));
}

}  // namespace internal

bool HostNameCertificateVerifier::Verify(
    CertificateVerificationRequest* internal_request,
    std::function<void()> callback) {
  GPR_ASSERT(internal_request != nullptr);
  grpc_tls_custom_verification_check_request* request =
      &internal_request->request;
  // Extract the target name, and remove its port.
  const char* target_name = request->target_name;
  if (target_name == nullptr) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("Target name is not specified.");
    return false;  // synchronous check
  }
  absl::string_view allocated_name;
  absl::string_view ignored_port;
  grpc_core::SplitHostPort(target_name, &allocated_name, &ignored_port);
  if (allocated_name.empty()) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("Failed to split hostname and port.");
    return false;  // synchronous check
  }
  // IPv6 zone-id should not be included in comparisons.
  const size_t zone_id = allocated_name.find('%');
  if (zone_id != absl::string_view::npos) {
    allocated_name.remove_suffix(allocated_name.size() - zone_id);
  }
  // Perform the hostname check.
  // First check the DNS field. We allow prefix or suffix wildcard matching.
  char** dns_names = request->peer_info.san_names.dns_names;
  size_t dns_names_size = request->peer_info.san_names.dns_names_size;
  if (dns_names != nullptr && dns_names_size > 0) {
    for (int i = 0; i < dns_names_size; ++i) {
      const char* dns_name = dns_names[i];
      if (internal::VerifyIdentityAsHostname(dns_name, allocated_name)) {
        request->status = GRPC_STATUS_OK;
        return false;  // synchronous check
      }
    }
  }
  // Then check the IP address. We only allow exact matching.
  char** ip_names = request->peer_info.san_names.ip_names;
  size_t ip_names_size = request->peer_info.san_names.ip_names_size;
  if (ip_names != nullptr && ip_names_size > 0) {
    for (int i = 0; i < ip_names_size; ++i) {
      const char* ip_name = ip_names[i];
      if (allocated_name == ip_name) {
        request->status = GRPC_STATUS_OK;
        return false;  // synchronous check
      }
    }
  }
  // If there's no SAN, try the CN.
  if (dns_names_size == 0) {
    const char* common_name = request->peer_info.common_name;
    if (internal::VerifyIdentityAsHostname(common_name, allocated_name)) {
      request->status = GRPC_STATUS_OK;
      return false;  // synchronous check
    }
  }
  request->status = GRPC_STATUS_UNAUTHENTICATED;
  request->error_details = gpr_strdup("Hostname Verification Check failed.");
  return false;  // synchronous check
}

}  // namespace grpc_core

//
// Wrapper APIs declared in grpc_security.h
//

int grpc_tls_certificate_verifier_verify(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback,
    void* callback_arg) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::CertificateVerificationRequest* internal_request =
      reinterpret_cast<grpc_core::CertificateVerificationRequest*>(request);
  std::function<void()> async_done = [callback, request, callback_arg] {
    callback(request, callback_arg);
  };
  return verifier->Verify(internal_request, async_done);
}

void grpc_tls_certificate_verifier_cancel(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request) {
  grpc_core::ExecCtx exec_ctx;
  grpc_core::CertificateVerificationRequest* internal_request =
      reinterpret_cast<grpc_core::CertificateVerificationRequest*>(request);
  verifier->Cancel(internal_request);
}

grpc_tls_certificate_verifier* grpc_tls_certificate_verifier_external_create(
    grpc_tls_certificate_verifier_external* external_verifier) {
  grpc_core::ExecCtx exec_ctx;
  return new grpc_core::ExternalCertificateVerifier(external_verifier);
}

grpc_tls_certificate_verifier*
grpc_tls_certificate_verifier_host_name_create() {
  grpc_core::ExecCtx exec_ctx;
  return new grpc_core::HostNameCertificateVerifier();
}

void grpc_tls_certificate_verifier_release(
    grpc_tls_certificate_verifier* verifier) {
  GRPC_API_TRACE("grpc_tls_certificate_verifier_release(verifier=%p)", 1,
                 (verifier));
  grpc_core::ExecCtx exec_ctx;
  if (verifier != nullptr) verifier->Unref();
}
