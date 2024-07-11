//
//
// Copyright 2015-2016 gRPC authors.
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

#include "src/core/lib/security/credentials/composite/composite_credentials.h"

#include <cstring>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/metadata_batch.h"

//
// grpc_composite_channel_credentials
//

grpc_core::UniqueTypeName grpc_composite_channel_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Composite");
  return kFactory.Create();
}

// -- Composite call credentials. --

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_composite_call_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs* args) {
  auto self = Ref();
  return TrySeqIter(
      inner_.begin(), inner_.end(), std::move(initial_metadata),
      [self, args](const grpc_core::RefCountedPtr<grpc_call_credentials>& creds,
                   grpc_core::ClientMetadataHandle initial_metadata) {
        return creds->GetRequestMetadata(std::move(initial_metadata), args);
      });
}

grpc_core::UniqueTypeName grpc_composite_call_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Composite");
  return kFactory.Create();
}

std::string grpc_composite_call_credentials::debug_string() {
  std::vector<std::string> outputs;
  for (auto& inner_cred : inner_) {
    outputs.emplace_back(inner_cred->debug_string());
  }
  return absl::StrCat("CompositeCallCredentials{", absl::StrJoin(outputs, ","),
                      "}");
}

static size_t get_creds_array_size(const grpc_call_credentials* creds,
                                   bool is_composite) {
  return is_composite
             ? static_cast<const grpc_composite_call_credentials*>(creds)
                   ->inner()
                   .size()
             : 1;
}

void grpc_composite_call_credentials::push_to_inner(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds, bool is_composite) {
  if (!is_composite) {
    inner_.push_back(std::move(creds));
    return;
  }
  auto composite_creds =
      static_cast<grpc_composite_call_credentials*>(creds.get());
  for (size_t i = 0; i < composite_creds->inner().size(); ++i) {
    inner_.push_back(composite_creds->inner_[i]);
  }
}

grpc_composite_call_credentials::grpc_composite_call_credentials(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds1,
    grpc_core::RefCountedPtr<grpc_call_credentials> creds2) {
  const bool creds1_is_composite =
      creds1->type() == grpc_composite_call_credentials::Type();
  const bool creds2_is_composite =
      creds2->type() == grpc_composite_call_credentials::Type();
  const size_t size = get_creds_array_size(creds1.get(), creds1_is_composite) +
                      get_creds_array_size(creds2.get(), creds2_is_composite);
  inner_.reserve(size);
  push_to_inner(std::move(creds1), creds1_is_composite);
  push_to_inner(std::move(creds2), creds2_is_composite);
  min_security_level_ = GRPC_SECURITY_NONE;
  for (size_t i = 0; i < inner_.size(); ++i) {
    if (static_cast<int>(min_security_level_) <
        static_cast<int>(inner_[i]->min_security_level())) {
      min_security_level_ = inner_[i]->min_security_level();
    }
  }
}

static grpc_core::RefCountedPtr<grpc_call_credentials>
composite_call_credentials_create(
    grpc_core::RefCountedPtr<grpc_call_credentials> creds1,
    grpc_core::RefCountedPtr<grpc_call_credentials> creds2) {
  return grpc_core::MakeRefCounted<grpc_composite_call_credentials>(
      std::move(creds1), std::move(creds2));
}

grpc_call_credentials* grpc_composite_call_credentials_create(
    grpc_call_credentials* creds1, grpc_call_credentials* creds2,
    void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_composite_call_credentials_create(creds1=" << creds1
      << ", creds2=" << creds2 << ", reserved=" << reserved << ")";
  CHECK_EQ(reserved, nullptr);
  CHECK_NE(creds1, nullptr);
  CHECK_NE(creds2, nullptr);

  return composite_call_credentials_create(creds1->Ref(), creds2->Ref())
      .release();
}

// -- Composite channel credentials. --

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_composite_channel_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target, grpc_core::ChannelArgs* args) {
  CHECK(inner_creds_ != nullptr);
  CHECK(call_creds_ != nullptr);
  // If we are passed a call_creds, create a call composite to pass it
  // downstream.
  if (call_creds != nullptr) {
    return inner_creds_->create_security_connector(
        composite_call_credentials_create(call_creds_, std::move(call_creds)),
        target, args);
  } else {
    return inner_creds_->create_security_connector(call_creds_, target, args);
  }
}

grpc_channel_credentials* grpc_composite_channel_credentials_create(
    grpc_channel_credentials* channel_creds, grpc_call_credentials* call_creds,
    void* reserved) {
  CHECK(channel_creds != nullptr && call_creds != nullptr &&
        reserved == nullptr);
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_composite_channel_credentials_create(channel_creds="
      << channel_creds << ", call_creds=" << call_creds
      << ", reserved=" << reserved << ")";
  return new grpc_composite_channel_credentials(channel_creds->Ref(),
                                                call_creds->Ref());
}
