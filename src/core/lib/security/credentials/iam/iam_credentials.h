/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_IAM_IAM_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_IAM_IAM_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/lib/security/credentials/credentials.h"

class grpc_google_iam_credentials : public grpc_call_credentials {
 public:
  grpc_google_iam_credentials(const char* token,
                              const char* authority_selector);

  grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
  GetRequestMetadata(grpc_core::ClientMetadataHandle initial_metadata,
                     const GetRequestMetadataArgs* args) override;

  std::string debug_string() override { return debug_string_; }

  static const char* Type();

  const char* type() const override { return Type(); }

 private:
  int cmp_impl(const grpc_call_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return grpc_core::QsortCompare(
        static_cast<const grpc_call_credentials*>(this), other);
  }

  const absl::optional<grpc_core::Slice> token_;
  const grpc_core::Slice authority_selector_;
  const std::string debug_string_;
};

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_IAM_IAM_CREDENTIALS_H */
