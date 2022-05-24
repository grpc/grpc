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

#include <string.h>

#include <string>
#include <utility>

#include "absl/strings/string_view.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/security/credentials/tls/tls_utils.h"
#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

//
// ExternalCertificateVerifier
//

bool ExternalCertificateVerifier::Verify(
    grpc_tls_custom_verification_check_request* request,
    std::function<void(absl::Status)> callback, absl::Status* sync_status) {
  {
    MutexLock lock(&mu_);
    request_map_.emplace(request, std::move(callback));
  }
  // Invoke the caller-specified verification logic embedded in
  // external_verifier_.
  grpc_status_code status_code = GRPC_STATUS_OK;
  char* error_details = nullptr;
  bool is_done = external_verifier_->verify(external_verifier_->user_data,
                                            request, &OnVerifyDone, this,
                                            &status_code, &error_details);
  if (is_done) {
    if (status_code != GRPC_STATUS_OK) {
      *sync_status = absl::Status(static_cast<absl::StatusCode>(status_code),
                                  error_details);
    }
    MutexLock lock(&mu_);
    request_map_.erase(request);
  }
  gpr_free(error_details);
  return is_done;
}

UniqueTypeName ExternalCertificateVerifier::type() const {
  static UniqueTypeName::Factory kFactory("External");
  return kFactory.Create();
}

void ExternalCertificateVerifier::OnVerifyDone(
    grpc_tls_custom_verification_check_request* request, void* callback_arg,
    grpc_status_code status, const char* error_details) {
  ExecCtx exec_ctx;
  auto* self = static_cast<ExternalCertificateVerifier*>(callback_arg);
  std::function<void(absl::Status)> callback;
  {
    MutexLock lock(&self->mu_);
    auto it = self->request_map_.find(request);
    if (it != self->request_map_.end()) {
      callback = std::move(it->second);
      self->request_map_.erase(it);
    }
  }
  if (callback != nullptr) {
    absl::Status return_status = absl::OkStatus();
    if (status != GRPC_STATUS_OK) {
      return_status =
          absl::Status(static_cast<absl::StatusCode>(status), error_details);
    }
    callback(return_status);
  }
}

//
// NoOpCertificateVerifier
//

UniqueTypeName NoOpCertificateVerifier::type() const {
  static UniqueTypeName::Factory kFactory("NoOp");
  return kFactory.Create();
}

//
// HostNameCertificateVerifier
//

bool HostNameCertificateVerifier::Verify(
    grpc_tls_custom_verification_check_request* request,
    std::function<void(absl::Status)>, absl::Status* sync_status) {
  GPR_ASSERT(request != nullptr);
  // Extract the target name, and remove its port.
  const char* target_name = request->target_name;
  if (target_name == nullptr) {
    *sync_status = absl::Status(absl::StatusCode::kUnauthenticated,
                                "Target name is not specified.");
    return true;  // synchronous check
  }
  absl::string_view target_host;
  absl::string_view ignored_port;
  SplitHostPort(target_name, &target_host, &ignored_port);
  if (target_host.empty()) {
    *sync_status = absl::Status(absl::StatusCode::kUnauthenticated,
                                "Failed to split hostname and port.");
    return true;  // synchronous check
  }
  // IPv6 zone-id should not be included in comparisons.
  const size_t zone_id = target_host.find('%');
  if (zone_id != absl::string_view::npos) {
    target_host.remove_suffix(target_host.size() - zone_id);
  }
  // Perform the hostname check.
  // First check the DNS field. We allow prefix or suffix wildcard matching.
  char** dns_names = request->peer_info.san_names.dns_names;
  size_t dns_names_size = request->peer_info.san_names.dns_names_size;
  if (dns_names != nullptr && dns_names_size > 0) {
    for (size_t i = 0; i < dns_names_size; ++i) {
      const char* dns_name = dns_names[i];
      // We are using the target name sent from the client as a matcher to match
      // against identity name on the peer cert.
      if (VerifySubjectAlternativeName(dns_name, std::string(target_host))) {
        return true;  // synchronous check
      }
    }
  }
  // Then check the IP address. We only allow exact matching.
  char** ip_names = request->peer_info.san_names.ip_names;
  size_t ip_names_size = request->peer_info.san_names.ip_names_size;
  if (ip_names != nullptr && ip_names_size > 0) {
    for (size_t i = 0; i < ip_names_size; ++i) {
      const char* ip_name = ip_names[i];
      if (target_host == ip_name) {
        return true;  // synchronous check
      }
    }
  }
  // If there's no SAN, try the CN.
  if (dns_names_size == 0) {
    const char* common_name = request->peer_info.common_name;
    // We are using the target name sent from the client as a matcher to match
    // against identity name on the peer cert.
    if (common_name != nullptr &&
        VerifySubjectAlternativeName(common_name, std::string(target_host))) {
      return true;  // synchronous check
    }
  }
  *sync_status = absl::Status(absl::StatusCode::kUnauthenticated,
                              "Hostname Verification Check failed.");
  return true;  // synchronous check
}

UniqueTypeName HostNameCertificateVerifier::type() const {
  static UniqueTypeName::Factory kFactory("Hostname");
  return kFactory.Create();
}

}  // namespace grpc_core

//
// Wrapper APIs declared in grpc_security.h
//

int grpc_tls_certificate_verifier_verify(
    grpc_tls_certificate_verifier* verifier,
    grpc_tls_custom_verification_check_request* request,
    grpc_tls_on_custom_verification_check_done_cb callback, void* callback_arg,
    grpc_status_code* sync_status, char** sync_error_details) {
  grpc_core::ExecCtx exec_ctx;
  std::function<void(absl::Status)> async_cb =
      [callback, request, callback_arg](absl::Status async_status) {
        callback(request, callback_arg,
                 static_cast<grpc_status_code>(async_status.code()),
                 std::string(async_status.message()).c_str());
      };
  absl::Status sync_status_cpp;
  bool is_done = verifier->Verify(request, async_cb, &sync_status_cpp);
  if (is_done) {
    if (!sync_status_cpp.ok()) {
      *sync_status = static_cast<grpc_status_code>(sync_status_cpp.code());
      *sync_error_details =
          gpr_strdup(std::string(sync_status_cpp.message()).c_str());
    }
  }
  return is_done;
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

grpc_tls_certificate_verifier* grpc_tls_certificate_verifier_no_op_create() {
  grpc_core::ExecCtx exec_ctx;
  return new grpc_core::NoOpCertificateVerifier();
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
