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

#ifndef GRPCPP_SECURITY_TLS_CERTIFICATE_PROVIDER_H
#define GRPCPP_SECURITY_TLS_CERTIFICATE_PROVIDER_H

#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpcpp/support/config.h>

#include <memory>
#include <vector>

//typedef struct grpc_tls_certificate_provider grpc_tls_certificate_provider;

namespace grpc {
namespace experimental {

class CertificateProviderInterface {
 public:
  virtual ~CertificateProviderInterface() = default;
  virtual grpc_tls_certificate_provider* c_provider() = 0;
};

class StaticDataCertificateProvider : public CertificateProviderInterface {
 public:
  StaticDataCertificateProvider(const std::string& root_certificate,
                                const std::string& private_key,
                                const std::string& identity_certificate);

  ~StaticDataCertificateProvider();

  grpc_tls_certificate_provider* c_provider() override { return c_provider_; }

 private:
  grpc_tls_certificate_provider* c_provider_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_TLS_CERTIFICATE_PROVIDER_H
