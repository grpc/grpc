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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/surface/api_trace.h"

namespace grpc_core {

StaticDataCertificateProvider::StaticDataCertificateProvider(
    std::string root_certificate, std::string private_key,
    std::string identity_certificate)
    : distributor_(MakeRefCounted<grpc_tls_certificate_distributor>()),
      root_certificate_(std::move(root_certificate)),
      private_key_(std::move(private_key)),
      identity_certificate_(std::move(identity_certificate)) {
  distributor_->SetWatchStatusCallback([this](std::string cert_name,
                                              bool root_being_watched,
                                              bool identity_being_watched) {
    if (identity_being_watched) {
      grpc_tls_certificate_distributor::PemKeyCertPairList identity_pairs;
      grpc_ssl_pem_key_cert_pair* ssl_pair =
          static_cast<grpc_ssl_pem_key_cert_pair*>(
              gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
      ssl_pair->private_key = gpr_strdup(private_key_.c_str());
      ssl_pair->cert_chain = gpr_strdup(identity_certificate_.c_str());
      identity_pairs.emplace_back(ssl_pair);
      if (root_being_watched) {
        distributor_->SetKeyMaterials(cert_name, root_certificate_,
                                      std::move(identity_pairs));
      } else {
        distributor_->SetKeyMaterials(cert_name, absl::nullopt,
                                      std::move(identity_pairs));
      }
    } else {
      if (root_being_watched) {
        distributor_->SetKeyMaterials(cert_name, root_certificate_,
                                      absl::nullopt);
      }
    }
  });
}

}  // namespace grpc_core

/** -- Wrapper APIs declared in grpc_security.h -- **/

grpc_tls_certificate_provider* grpc_tls_certificate_provider_static_data_create(
    const char* root_certificate, const char* private_key,
    const char* identity_certificate) {
  return new grpc_core::StaticDataCertificateProvider(
      root_certificate, private_key, identity_certificate);
}

void grpc_tls_certificate_provider_release(
    grpc_tls_certificate_provider* provider) {
  GRPC_API_TRACE("grpc_tls_certificate_provider_release(provider=%p)", 1,
                 (provider));
  grpc_core::ExecCtx exec_ctx;
  if (provider != nullptr) provider->Unref();
}
