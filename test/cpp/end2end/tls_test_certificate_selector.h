//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_TEST_CPP_END2END_TLS_TEST_CERTIFICATE_SELECTOR_H
#define GRPC_TEST_CPP_END2END_TLS_TEST_CERTIFICATE_SELECTOR_H

#include <grpc/event_engine/event_engine.h>
#include <grpcpp/security/tls_private_key_signer.h>
#include <grpcpp/support/status.h>
#include <openssl/pem.h>

#include <memory>
#include <variant>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_selector.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc {
namespace testing {

class SyncTestCertificateSelector : public grpc_core::CertificateSelector {
 public:
  SyncTestCertificateSelector(
      absl::string_view pem_cert_chain,
      std::variant<absl::string_view,
                   std::shared_ptr<grpc::experimental::PrivateKeySigner>>
          pem_private_key,
      absl::string_view sni)
      : pem_cert_chain_(pem_cert_chain),
        pem_private_key_(std::move(pem_private_key)),
        sni_(sni) {}

  std::variant<absl::StatusOr<SelectCertificateResult>,
               std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo& info,
                    OnSelectCertificateComplete) override;

  void Cancel(
      std::shared_ptr<AsyncCertificateSelectionHandle> /*handle*/) override {}

 private:
  absl::string_view pem_cert_chain_;
  std::variant<absl::string_view,
               std::shared_ptr<grpc::experimental::PrivateKeySigner>>
      pem_private_key_;
  absl::string_view sni_;
};

class AsyncTestCertificateSelector : public grpc_core::CertificateSelector {
 public:
  AsyncTestCertificateSelector(
      absl::string_view pem_cert_chain,
      std::variant<absl::string_view,
                   std::shared_ptr<grpc::experimental::PrivateKeySigner>>
          pem_private_key,
      absl::string_view sni, absl::Duration delay = absl::ZeroDuration())
      : pem_cert_chain_(pem_cert_chain),
        pem_private_key_(std::move(pem_private_key)),
        sni_(sni),
        delay_(delay) {}

  std::variant<absl::StatusOr<SelectCertificateResult>,
               std::shared_ptr<AsyncCertificateSelectionHandle>>
  SelectCertificate(const SelectCertificateInfo& info,
                    OnSelectCertificateComplete on_complete) override;

  void Cancel(std::shared_ptr<AsyncCertificateSelectionHandle> handle) override;

  bool WasCancelled();

 private:
  struct AsyncCertificateSelectionHandleInternal
      : public AsyncCertificateSelectionHandle {
    grpc_event_engine::experimental::EventEngine::TaskHandle task_handle;
  };

  absl::string_view pem_cert_chain_;
  std::variant<absl::string_view,
               std::shared_ptr<grpc::experimental::PrivateKeySigner>>
      pem_private_key_;
  absl::string_view sni_;
  absl::Duration delay_;
  std::atomic<bool> was_cancelled_{false};
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_TLS_TEST_CERTIFICATE_SELECTOR_H
