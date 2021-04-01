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
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

bool ExternalCertificateVerifier::Verify(
    grpc_tls_custom_verification_check_request* request,
    std::function<void()> callback) {
  grpc_core::ExecCtx exec_ctx;
  {
    grpc_core::MutexLock lock(&mu_);
    request_map_.emplace(request, std::move(callback));
  }
  // Invoke the caller-specified verification logic embedded in
  // external_verifier_.
  bool is_async = external_verifier_->verify(external_verifier_->user_data,
                                             request, &OnVerifyDone, this);
  if (!is_async) {
    grpc_core::MutexLock lock(&mu_);
    request_map_.erase(request);
  }
  return is_async;
}

void ExternalCertificateVerifier::OnVerifyDone(
    grpc_tls_custom_verification_check_request* request, void* user_data) {
  auto* self = static_cast<ExternalCertificateVerifier*>(user_data);
  grpc_core::MutexLock lock(&self->mu_);
  auto it = self->request_map_.find(request);
  if (it != self->request_map_.end()) {
    std::function<void()> callback = std::move(it->second);
    self->request_map_.erase(it->first);
    callback();
  }
}

namespace internal {

// TODO(ZhenLian): probably we can use VerifySubjectAlternativeName in
// tls_utils.h to replace this check.
static bool VerifyIdentityAsHostname(absl::string_view identity_name,
                                     absl::string_view target_name) {
  if (identity_name.empty()) return false;
  // Take care of '.' terminations.
  if (target_name.back() == '.') {
    target_name.remove_suffix(1);
  }
  if (identity_name.back() == '.') {
    identity_name.remove_suffix(1);
    if (identity_name.empty()) return false;
  }
  // Perfect match.
  if (absl::EqualsIgnoreCase(target_name, identity_name)) {
    return true;
  }
  if (identity_name.front() != '*') return false;
  // Wildchar subdomain matching.
  if (identity_name.size() < 3 || identity_name[1] != '.') {  // At least *.x
    gpr_log(GPR_ERROR, "Invalid wildchar identity_name.");
    return false;
  }
  size_t name_subdomain_pos = target_name.find('.');
  if (name_subdomain_pos == absl::string_view::npos) return false;
  if (name_subdomain_pos >= target_name.size() - 2) return false;
  absl::string_view name_subdomain =
      target_name.substr(name_subdomain_pos + 1);  // Starts after the dot.
  identity_name.remove_prefix(2);                  // Remove *.
  size_t dot = name_subdomain.find('.');
  if (dot == absl::string_view::npos || dot == name_subdomain.size() - 1) {
    gpr_log(GPR_ERROR, "Invalid toplevel subdomain: %s",
            std::string(name_subdomain).c_str());
    return false;
  }
  if (name_subdomain.back() == '.') {
    name_subdomain.remove_suffix(1);
  }
  return !identity_name.empty() &&
         absl::EqualsIgnoreCase(name_subdomain, identity_name);
}

}  // namespace internal

bool HostNameCertificateVerifier::Verify(
    grpc_tls_custom_verification_check_request* request,
    std::function<void()> callback) {
  GPR_ASSERT(request != nullptr);
  // Extract the target name, and remove its port.
  const char* target_name = request->target_name;
  if (target_name == nullptr) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("Target name is not specified.");
    return false; /*synchronous check*/
  }
  absl::string_view allocated_name;
  absl::string_view ignored_port;
  grpc_core::SplitHostPort(target_name, &allocated_name, &ignored_port);
  if (allocated_name.empty()) {
    request->status = GRPC_STATUS_UNAUTHENTICATED;
    request->error_details = gpr_strdup("Failed to split hostname and port.");
    return false; /*synchronous check*/
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
        return false; /* synchronous check */
      }
    }
  }
  // Then check the IP address. We only allow exact matching.
  char** ip_names = request->peer_info.san_names.ip_names;
  size_t ip_names_size = request->peer_info.san_names.ip_names_size;
  if (ip_names != nullptr && ip_names_size > 0) {
    for (int i = 0; i < ip_names_size; ++i) {
      std::string target_ip(allocated_name);
      const char* ip_name = ip_names[i];
      if (strcmp(ip_name, target_ip.c_str()) == 0) {
        request->status = GRPC_STATUS_OK;
        return false; /*synchronous check*/
      }
    }
  }
  // If there's no SAN, try the CN.
  if (dns_names_size == 0) {
    const char* common_name = request->peer_info.common_name;
    if (internal::VerifyIdentityAsHostname(common_name, allocated_name)) {
      request->status = GRPC_STATUS_OK;
      return false; /*synchronous check*/
    }
  }
  request->status = GRPC_STATUS_UNAUTHENTICATED;
  request->error_details = gpr_strdup("Hostname Verification Check failed.");
  return false; /* synchronous check */
}

}  // namespace grpc_core

/** -- Wrapper APIs declared in grpc_security.h -- **/

int grpc_tls_certificate_verifier_verify(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback,
    void* callback_arg) {
  std::function<void()> async_done = [callback, request, callback_arg] {
    callback(request, callback_arg);
  };
  return verifier->Verify(request, async_done);
}

void grpc_tls_certificate_verifier_cancel(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request) {
  verifier->Cancel(request);
}

grpc_tls_certificate_verifier* grpc_tls_certificate_verifier_external_create(
    grpc_tls_certificate_verifier_external* external_verifier) {
  return new grpc_core::ExternalCertificateVerifier(external_verifier);
}

grpc_tls_certificate_verifier*
grpc_tls_certificate_verifier_host_name_create() {
  return new grpc_core::HostNameCertificateVerifier();
}

void grpc_tls_certificate_verifier_release(
    grpc_tls_certificate_verifier* verifier) {
  GRPC_API_TRACE("grpc_tls_certificate_verifier_release(verifier=%p)", 1,
                 (verifier));
  grpc_core::ExecCtx exec_ctx;
  // This should work fine as-is, because the C++ delete operator will
  // automatically use the dtor for the concrete subclass of the instance.
  if (verifier != nullptr) verifier->Unref();
}
