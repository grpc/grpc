//
// Copyright 2024 gRPC authors.
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

#include "src/core/credentials/call/jwt_token_file/jwt_token_file_call_credentials.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "src/core/credentials/call/jwt_util.h"
#include "src/core/util/load_file.h"

namespace grpc_core {

class JwtTokenFileCallCredentials::FileReader final
    : public TokenFetcherCredentials::FetchRequest {
 public:
  FileReader(JwtTokenFileCallCredentials* creds,
             absl::AnyInvocable<void(
                 absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>)>
                 on_done)
      : creds_(creds), on_done_(std::move(on_done)) {
    creds->event_engine().Run(
        [self = RefAsSubclass<FileReader>()]() { self->ReadFile(); });
  }

  void Orphan() override {
    // Can't really do anything to cancel in this case.
    Unref();
  }

 private:
  void ReadFile() {
    auto contents = LoadFile(creds_->path_, /*add_null_terminator=*/false);
    if (!contents.ok()) {
      on_done_(contents.status());
      return;
    }
    absl::string_view body = contents->as_string_view();
    auto expiration_time = GetJwtExpirationTime(body);
    if (!expiration_time.ok()) {
      on_done_(expiration_time.status());
      return;
    }
    on_done_(MakeRefCounted<Token>(
        Slice::FromCopiedString(absl::StrCat("Bearer ", body)),
        *expiration_time));
  }

  JwtTokenFileCallCredentials* creds_;
  absl::AnyInvocable<void(
      absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>)>
      on_done_;
};

std::string JwtTokenFileCallCredentials::debug_string() {
  return absl::StrCat("JwtTokenFileCallCredentials(", path_, ")");
}

UniqueTypeName JwtTokenFileCallCredentials::Type() {
  static UniqueTypeName::Factory kFactory("JwtTokenFile");
  return kFactory.Create();
}

OrphanablePtr<TokenFetcherCredentials::FetchRequest>
JwtTokenFileCallCredentials::FetchToken(
    Timestamp /*deadline*/,
    absl::AnyInvocable<
        void(absl::StatusOr<RefCountedPtr<TokenFetcherCredentials::Token>>)>
        on_done) {
  return MakeOrphanable<FileReader>(this, std::move(on_done));
}

}  // namespace grpc_core
