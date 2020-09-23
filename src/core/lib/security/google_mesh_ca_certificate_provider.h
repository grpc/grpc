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

#ifndef GRPC_CORE_LIB_SECURITY_GOOGLE_MESH_CA_CERTIFICATE_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_GOOGLE_MESH_CA_CERTIFICATE_PROVIDER_H

#include <grpc/support/port_platform.h>

#include "absl/types/optional.h"

#include <grpc/grpc_security.h>

#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/security/certificate_provider.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_distributor.h"

namespace grpc_core {

class GoogleMeshCaCertificateProvider : public grpc_tls_certificate_provider {
 public:
  struct ParsedResult {
    std::string pem_root_certs;
    grpc_tls_certificate_distributor::PemKeyCertPairList pem_key_cert_pairs;
  };
  GoogleMeshCaCertificateProvider(std::string endpoint,
                                  grpc_channel_credentials* credentials,
                                  grpc_millis timeout,
                                  grpc_millis certificate_lifetime,
                                  grpc_millis renewal_grace_period,
                                  uint32_t key_size);

  ~GoogleMeshCaCertificateProvider();

  grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor()
      const override {
    return distributor_;
  }

 private:
  void StartCallLocked();
  static void OnCallComplete(void* arg, grpc_error* error);
  void OnCallComplete();
  absl::optional<std::string> GenerateCSR();
  bool GenerateRequestLocked();
  void WatchStatusCallback(std::string cert_name, bool root_being_watched,
                           bool identity_being_watched);
  static void OnRenewalTimer(void* arg, grpc_error* error);

  void ParseCertChain();

  Mutex mu_;
  std::string endpoint_;
  grpc_millis timeout_;
  grpc_millis certificate_lifetime_;
  grpc_millis renewal_grace_period_;
  uint32_t key_size_;
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  grpc_closure on_call_complete_;
  // Closure for certificate renewal
  grpc_closure on_renewal_timer_;
  // Timer to trigger certificate renewal
  grpc_timer renewal_timer_;
  grpc_channel* channel_ = nullptr;
  grpc_call* call_ = nullptr;
  grpc_metadata_array initial_metadata_recv_;
  grpc_metadata_array trailing_metadata_recv_;
  grpc_byte_buffer* request_payload_ = nullptr;
  grpc_byte_buffer* response_payload_ = nullptr;
  grpc_status_code status_ = GRPC_STATUS_OK;
  grpc_slice status_details_;
  // Private key in PEM format
  std::string private_key_;
  // Time at which the certificate was received
  grpc_millis time_of_certificate_ = GRPC_MILLIS_INF_FUTURE;
  BackOff backoff_;
  absl::optional<ParsedResult> parsed_result_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_GOOGLE_MESH_CA_CERTIFICATE_PROVIDER_H