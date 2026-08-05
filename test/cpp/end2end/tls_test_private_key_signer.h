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

#ifndef GRPC_TEST_CPP_END2END_TLS_TEST_PRIVATE_KEY_SIGNER_H
#define GRPC_TEST_CPP_END2END_TLS_TEST_PRIVATE_KEY_SIGNER_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpcpp/security/tls_private_key_signer.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>
// "Force" the definition of OPENSSL_IS_BORINGSSL.
#include <openssl/crypto.h>

#include <memory>
#include <string>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#if defined(OPENSSL_IS_BORINGSSL)

namespace grpc {
namespace testing {

class SyncTestPrivateKeySigner final
    : public grpc::experimental::PrivateKeySigner {
 public:
  enum class Mode { kSuccess, kError };

  explicit SyncTestPrivateKeySigner(absl::string_view private_key,
                                    Mode mode = Mode::kSuccess);

  std::variant<absl::StatusOr<std::string>,
               std::shared_ptr<grpc_core::PrivateKeySigner::AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete /*on_sign_complete*/) override;

  void Cancel(std::shared_ptr<grpc_core::PrivateKeySigner::AsyncSigningHandle>
              /*handle*/) override {}

 private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
  Mode mode_;
};

class AsyncTestPrivateKeySigner final
    : public grpc::experimental::PrivateKeySigner,
      public std::enable_shared_from_this<AsyncTestPrivateKeySigner> {
 public:
  enum class Mode { kSuccess, kDelayed, kCancellation, kError };

  explicit AsyncTestPrivateKeySigner(
      absl::string_view private_key, Mode mode = Mode::kSuccess,
      absl::Duration delay = absl::ZeroDuration());

  std::variant<absl::StatusOr<std::string>,
               std::shared_ptr<grpc_core::PrivateKeySigner::AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete on_sign_complete) override;

  void Cancel(std::shared_ptr<grpc_core::PrivateKeySigner::AsyncSigningHandle>
                  handle) override;

  bool WasCancelled() { return was_cancelled_.load(); }

 private:
  struct AsyncSigningHandleInternal
      : public grpc_core::PrivateKeySigner::AsyncSigningHandle {
    grpc_event_engine::experimental::EventEngine::TaskHandle task_handle;
  };

  bssl::UniquePtr<EVP_PKEY> pkey_;
  Mode mode_;
  absl::Duration delay_;
  std::atomic<bool> was_cancelled_{false};
};

}  // namespace testing
}  // namespace grpc

#endif  // OPENSSL_IS_BORINGSSL
#endif  // GRPC_TEST_CPP_END2END_TLS_TEST_PRIVATE_KEY_SIGNER_H
