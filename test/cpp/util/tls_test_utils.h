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

#ifndef GRPC_TEST_CPP_UTIL_TLS_TEST_UTILS_H
#define GRPC_TEST_CPP_UTIL_TLS_TEST_UTILS_H

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/security/server_credentials.h>

namespace {

using ::grpc::experimental::CertificateVerifier;
using ::grpc::experimental::ExternalCertificateVerifier;
using ::grpc::experimental::HostNameCertificateVerifier;
using ::grpc::experimental::TlsCustomVerificationCheckRequest;

}  // namespace

namespace grpc {
namespace testing {

class SyncCertificateVerifier : public ExternalCertificateVerifier {
 public:
  SyncCertificateVerifier(bool success)
      : ExternalCertificateVerifier(),
        success_(success),
        hostname_verifier_(std::make_shared<HostNameCertificateVerifier>()) {}

  bool Verify(TlsCustomVerificationCheckRequest* request,
              std::function<void(grpc::Status)> callback,
              grpc::Status* sync_status) override;

  void Cancel(TlsCustomVerificationCheckRequest* request) override {}

 private:
  void AdditionalSyncCheck(bool success, grpc::Status* sync_status);

  bool success_ = false;
  std::shared_ptr<HostNameCertificateVerifier> hostname_verifier_;
};

class AsyncCertificateVerifier : public ExternalCertificateVerifier {
 public:
  AsyncCertificateVerifier(bool success)
      : ExternalCertificateVerifier(),
        success_(success),
        hostname_verifier_(std::make_shared<HostNameCertificateVerifier>()) {}

  ~AsyncCertificateVerifier();

  bool Verify(TlsCustomVerificationCheckRequest* request,
              std::function<void(grpc::Status)> callback,
              grpc::Status* sync_status) override;

  void Cancel(TlsCustomVerificationCheckRequest* request) override {}

 private:
  struct ThreadArgs {
    AsyncCertificateVerifier* self = nullptr;
    TlsCustomVerificationCheckRequest* request = nullptr;
    // We don't use a pointer but force a copy here, because the original status
    // could go away.
    grpc::Status status;
  };

  static void AdditionalAsyncCheck(void* arg);

  bool success_ = false;
  std::shared_ptr<HostNameCertificateVerifier> hostname_verifier_;
  std::mutex mu_;
  std::map<TlsCustomVerificationCheckRequest*,
           std::function<void(grpc::Status)>>
      request_map_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_TEST_CONFIG_H