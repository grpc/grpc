//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/lib/security/credentials/iam/iam_credentials.h"

#include <grpc/support/port_platform.h>
#include <stdlib.h>

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/util/ref_counted_ptr.h"

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_google_iam_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs*) {
  if (token_.has_value()) {
    initial_metadata->Append(
        GRPC_IAM_AUTHORIZATION_TOKEN_METADATA_KEY, token_->Ref(),
        [](absl::string_view, const grpc_core::Slice&) { abort(); });
  }
  initial_metadata->Append(
      GRPC_IAM_AUTHORITY_SELECTOR_METADATA_KEY, authority_selector_.Ref(),
      [](absl::string_view, const grpc_core::Slice&) { abort(); });
  return grpc_core::Immediate(std::move(initial_metadata));
}

grpc_google_iam_credentials::grpc_google_iam_credentials(
    const char* token, const char* authority_selector)
    : token_(token == nullptr ? absl::optional<grpc_core::Slice>()
                              : grpc_core::Slice::FromCopiedString(token)),
      authority_selector_(
          grpc_core::Slice::FromCopiedString(authority_selector)),
      debug_string_(absl::StrFormat(
          "GoogleIAMCredentials{Token:%s,AuthoritySelector:%s}",
          token != nullptr ? "present" : "absent", authority_selector)) {}

grpc_core::UniqueTypeName grpc_google_iam_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Iam");
  return kFactory.Create();
}

grpc_call_credentials* grpc_google_iam_credentials_create(
    const char* token, const char* authority_selector, void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_TRACE_LOG(api, INFO) << "grpc_iam_credentials_create(token=" << token
                            << ", authority_selector=" << authority_selector
                            << ", reserved=" << reserved << ")";
  CHECK_EQ(reserved, nullptr);
  CHECK_NE(token, nullptr);
  CHECK_NE(authority_selector, nullptr);
  return grpc_core::MakeRefCounted<grpc_google_iam_credentials>(
             token, authority_selector)
      .release();
}
