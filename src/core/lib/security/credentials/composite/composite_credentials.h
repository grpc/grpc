/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/transport/transport.h"

/* -- Composite channel credentials. -- */

class grpc_composite_channel_credentials : public grpc_channel_credentials {
 public:
  grpc_composite_channel_credentials(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds)
      : inner_creds_(std::move(channel_creds)),
        call_creds_(std::move(call_creds)) {}

  ~grpc_composite_channel_credentials() override = default;

  grpc_core::RefCountedPtr<grpc_channel_credentials>
  duplicate_without_call_credentials() override {
    return inner_creds_;
  }

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* target, grpc_core::ChannelArgs* args) override;

  grpc_core::ChannelArgs update_arguments(
      grpc_core::ChannelArgs args) override {
    return inner_creds_->update_arguments(std::move(args));
  }

  grpc_core::UniqueTypeName type() const override;

  const grpc_channel_credentials* inner_creds() const {
    return inner_creds_.get();
  }
  const grpc_call_credentials* call_creds() const { return call_creds_.get(); }
  grpc_call_credentials* mutable_call_creds() { return call_creds_.get(); }

 private:
  int cmp_impl(const grpc_channel_credentials* other) const override {
    auto* o = static_cast<const grpc_composite_channel_credentials*>(other);
    int r = inner_creds_->cmp(o->inner_creds_.get());
    if (r != 0) return r;
    return call_creds_->cmp(o->call_creds_.get());
  }

  grpc_core::RefCountedPtr<grpc_channel_credentials> inner_creds_;
  grpc_core::RefCountedPtr<grpc_call_credentials> call_creds_;
};

/* -- Composite call credentials. -- */

class grpc_composite_call_credentials : public grpc_call_credentials {
 public:
  using CallCredentialsList =
      std::vector<grpc_core::RefCountedPtr<grpc_call_credentials>>;

  grpc_composite_call_credentials(
      grpc_core::RefCountedPtr<grpc_call_credentials> creds1,
      grpc_core::RefCountedPtr<grpc_call_credentials> creds2);
  ~grpc_composite_call_credentials() override = default;

  grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
  GetRequestMetadata(grpc_core::ClientMetadataHandle initial_metadata,
                     const GetRequestMetadataArgs* args) override;

  grpc_security_level min_security_level() const override {
    return min_security_level_;
  }

  const CallCredentialsList& inner() const { return inner_; }
  std::string debug_string() override;

  static grpc_core::UniqueTypeName Type();

  grpc_core::UniqueTypeName type() const override { return Type(); }

 private:
  int cmp_impl(const grpc_call_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return grpc_core::QsortCompare(
        static_cast<const grpc_call_credentials*>(this), other);
  }

  void push_to_inner(grpc_core::RefCountedPtr<grpc_call_credentials> creds,
                     bool is_composite);
  grpc_security_level min_security_level_;
  CallCredentialsList inner_;
};

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H \
        */
