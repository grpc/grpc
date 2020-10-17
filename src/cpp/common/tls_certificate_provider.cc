/*
 *
 * Copyright 2020 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpcpp/security/tls_certificate_provider.h>

#include "absl/container/inlined_vector.h"

namespace grpc {
namespace experimental {

StaticDataCertificateProvider::StaticDataCertificateProvider(
    const std::string& root_certificate, const std::string& private_key,
    const std::string& identity_certificate) {
  c_provider_ = grpc_tls_certificate_provider_static_data_create(
      root_certificate.c_str(), private_key.c_str(),
      identity_certificate.c_str());
};

StaticDataCertificateProvider::~StaticDataCertificateProvider() {
  grpc_tls_certificate_provider_release(c_provider_);
};

}  // namespace experimental
}  // namespace grpc
