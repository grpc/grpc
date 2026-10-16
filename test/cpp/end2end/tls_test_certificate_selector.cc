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

#include "test/cpp/end2end/tls_test_certificate_selector.h"

#include <grpc/event_engine/event_engine.h>
#include <grpcpp/security/tls_private_key_signer.h>

#include "src/core/credentials/transport/tls/grpc_tls_certificate_selector.h"
// "Force" the definition of OPENSSL_IS_BORINGSSL.
#include <openssl/crypto.h>

#include <memory>

#include "src/core/util/down_cast.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#if defined(OPENSSL_IS_BORINGSSL)

namespace grpc {
namespace testing {

using SelectCertificateResult =
    grpc_core::CertificateSelector::SelectCertificateResult;
using AsyncCertificateSelectionHandle =
    grpc_core::CertificateSelector::AsyncCertificateSelectionHandle;

std::variant<absl::StatusOr<SelectCertificateResult>,
             std::shared_ptr<AsyncCertificateSelectionHandle>>
SyncTestCertificateSelector::SelectCertificate(
    const SelectCertificateInfo& info, OnSelectCertificateComplete) {
  if (info.sni != sni_) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Expected SNI to be %s, got %s", sni_, info.sni));
  }
  absl::StatusOr<SelectCertificateResult> result =
      CreateSelectCertificateResult(pem_cert_chain_, pem_private_key_);
  CHECK_OK(result);
  return std::move(result);
}

std::variant<absl::StatusOr<SelectCertificateResult>,
             std::shared_ptr<AsyncCertificateSelectionHandle>>
AsyncTestCertificateSelector::SelectCertificate(
    const SelectCertificateInfo& info,
    OnSelectCertificateComplete on_complete) {
  auto event_engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  auto handle = std::make_shared<AsyncCertificateSelectionHandleInternal>();
  handle->task_handle = event_engine->RunAfter(
      std::chrono::nanoseconds(absl::ToInt64Nanoseconds(delay_)),
      [this, info, on_complete = std::move(on_complete)]() mutable {
        if (info.sni != sni_) {
          on_complete(absl::InvalidArgumentError(absl::StrFormat(
              "Expected SNI to be %s, got %s", sni_, info.sni)));
          return;
        }
        on_complete(
            CreateSelectCertificateResult(pem_cert_chain_, pem_private_key_));
      });
  return handle;
}

void AsyncTestCertificateSelector::Cancel(
    std::shared_ptr<AsyncCertificateSelectionHandle> handle) {
  auto event_engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  auto* internal_handle =
      grpc_core::DownCast<AsyncCertificateSelectionHandleInternal*>(
          handle.get());
  if (event_engine->Cancel(internal_handle->task_handle)) {
    // Only flip this if the event engine cancellation was successful.
    was_cancelled_.store(true);
  }
}

bool AsyncTestCertificateSelector::WasCancelled() {
  return was_cancelled_.load();
}

}  // namespace testing
}  // namespace grpc

#endif  // OPENSSL_IS_BORINGSSL
