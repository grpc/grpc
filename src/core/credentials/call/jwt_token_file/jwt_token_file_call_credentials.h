//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_JWT_TOKEN_FILE_JWT_TOKEN_FILE_CALL_CREDENTIALS_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_JWT_TOKEN_FILE_JWT_TOKEN_FILE_CALL_CREDENTIALS_H

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>

#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/credentials/call/token_fetcher/token_fetcher_credentials.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"

namespace grpc_core {

// JWT token file call credentials.
// See gRFC A97 (https://github.com/grpc/proposal/pull/492).
class JwtTokenFileCallCredentials : public TokenFetcherCredentials {
 public:
  explicit JwtTokenFileCallCredentials(
      absl::string_view path,
      std::shared_ptr<grpc_event_engine::experimental::EventEngine>
          event_engine = nullptr)
      : TokenFetcherCredentials(std::move(event_engine)), path_(path) {}

  std::string debug_string() override;

  static UniqueTypeName Type();

  UniqueTypeName type() const override { return Type(); }

  absl::string_view path() const { return path_; }

 private:
  class FileReader;

  OrphanablePtr<FetchRequest> FetchToken(
      Timestamp /*deadline*/,
      absl::AnyInvocable<
          void(absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>)>
          on_done) final;

  int cmp_impl(const grpc_call_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return QsortCompare(static_cast<const grpc_call_credentials*>(this), other);
  }

  std::string path_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_JWT_TOKEN_FILE_JWT_TOKEN_FILE_CALL_CREDENTIALS_H
