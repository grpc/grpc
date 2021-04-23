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

bool ExternalCertificateVerifier::Verify(
    grpc_tls_custom_verification_check_request* request,
    std::function<void()> callback) {
  {
    MutexLock lock(&mu_);
    request_map_.emplace(request, std::move(callback));
  }
  // Invoke the caller-specified verification logic embedded in
  // external_verifier_.
  bool is_done = external_verifier_->verify(external_verifier_->user_data,
                                            request, &OnVerifyDone, this);
  if (is_done) {
    MutexLock lock(&mu_);
    request_map_.erase(request);
  }
  return is_done;
}

void ExternalCertificateVerifier::OnVerifyDone(
    grpc_tls_custom_verification_check_request* request, void* user_data) {
  ExecCtx exec_ctx;
  auto* self = static_cast<ExternalCertificateVerifier*>(user_data);
  std::function<void()> callback;
  {
    MutexLock lock(&self->mu_);
    auto it = self->request_map_.find(request);
    if (it != self->request_map_.end()) {
      callback = std::move(it->second);
      self->request_map_.erase(it);
    }
  }
  if (callback != nullptr) callback();
}

bool HostNameCertificateVerifier::Verify(
    grpc_tls_custom_verification_check_request* request,
    std::function<void()> callback) {
  GPR_ASSERT(request != nullptr);
  // Extract the target name, and remove its port.
  const char* target_name = request->target_name;
  if (target_name == nullptr) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("Target name is not specified.");
    return true;  // synchronous check
  }
  absl::string_view allocated_name;
  absl::string_view ignored_port;
  SplitHostPort(target_name, &allocated_name, &ignored_port);
  if (allocated_name.empty()) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("Failed to split hostname and port.");
    return true;  // synchronous check
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
      // We are using the target name sent from the client as a matcher to match
      // against identity name on the peer cert.
      if (VerifySubjectAlternativeName(dns_name, std::string(allocated_name))) {
        request->status = GRPC_STATUS_OK;
        return true;  // synchronous check
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
        return true;  // synchronous check
      }
    }
  }
  // If there's no SAN, try the CN.
  if (dns_names_size == 0) {
    const char* common_name = request->peer_info.common_name;
    // We are using the target name sent from the client as a matcher to match
    // against identity name on the peer cert.
    if (VerifySubjectAlternativeName(common_name,
                                     std::string(allocated_name))) {
      request->status = GRPC_STATUS_OK;
      return true;  // synchronous check
    }
  }
  request->status = GRPC_STATUS_UNAUTHENTICATED;
  request->error_details = gpr_strdup("Hostname Verification Check failed.");
  return true;  // synchronous check
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
  std::function<void()> async_cb = [callback, request, callback_arg] {
    callback(request, callback_arg);
  };
  return verifier->Verify(request, async_cb);
}

void grpc_tls_certificate_verifier_cancel(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request) {
  grpc_core::ExecCtx exec_ctx;
  verifier->Cancel(request);
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
