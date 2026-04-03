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

#ifndef GRPC_TEST_CORE_TSI_PRIVATE_KEY_SIGNER_TEST_UTIL_H
#define GRPC_TEST_CORE_TSI_PRIVATE_KEY_SIGNER_TEST_UTIL_H

#include <grpc/event_engine/event_engine.h>
#include <grpc/private_key_signer.h>
#include <openssl/digest.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/base.h>

#include <atomic>
#include <memory>
#include <string>
#include <variant>

#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#if defined(OPENSSL_IS_BORINGSSL)

namespace grpc_core {
namespace testing {

class SyncTestPrivateKeySigner final : public PrivateKeySigner {
 public:
  enum class Mode { kSuccess, kError, kInvalidSignature };

  explicit SyncTestPrivateKeySigner(absl::string_view private_key,
                                    Mode mode = Mode::kSuccess);

  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete /*on_sign_complete*/) override;

  void Cancel(std::shared_ptr<AsyncSigningHandle> /*handle*/) override;

 private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
  Mode mode_;
};

class AsyncTestPrivateKeySigner final
    : public PrivateKeySigner,
      public std::enable_shared_from_this<AsyncTestPrivateKeySigner> {
 public:
  enum class Mode { kSuccess, kError, kCancellation };

  explicit AsyncTestPrivateKeySigner(
      absl::string_view private_key,
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine,
      Mode mode = Mode::kSuccess);

  std::variant<absl::StatusOr<std::string>, std::shared_ptr<AsyncSigningHandle>>
  Sign(absl::string_view data_to_sign, SignatureAlgorithm signature_algorithm,
       OnSignComplete on_sign_complete) override;

  void Cancel(std::shared_ptr<AsyncSigningHandle> /*handle*/) override;

  bool WasCancelled();

 private:
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
  Mode mode_;
  std::atomic<bool> was_cancelled_{false};
};

#endif  // OPENSSL_IS_BORINGSSL

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TSI_PRIVATE_KEY_SIGNER_TEST_UTIL_H
