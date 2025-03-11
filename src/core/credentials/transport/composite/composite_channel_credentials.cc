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

#include "src/core/credentials/transport/composite/composite_channel_credentials.h"

#include <grpc/support/port_platform.h>

#include <cstring>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/credentials/call/composite/composite_call_credentials.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/util/ref_counted_ptr.h"

grpc_core::UniqueTypeName grpc_composite_channel_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Composite");
  return kFactory.Create();
}

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
        grpc_core::MakeRefCounted<grpc_composite_call_credentials>(
            call_creds_, std::move(call_creds)),
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
