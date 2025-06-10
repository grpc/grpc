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

#ifndef GRPC_SRC_CORE_CREDENTIALS_CALL_DUAL_DUAL_CALL_CREDENTIALS_H
#define GRPC_SRC_CORE_CREDENTIALS_CALL_DUAL_DUAL_CALL_CREDENTIALS_H

#include <grpc/grpc_security.h>

#include "absl/status/statusor.h"
#include "src/core/credentials/call/call_credentials.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

/// A grpc_call_credentials implementation that uses two underlying
/// credentials: one for TLS and one for ALTS.
//  The implementation will pick the right credentials based on the auth
//  context's GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME property.
class DualCallCredentials : public grpc_call_credentials {
 public:
  DualCallCredentials(RefCountedPtr<grpc_call_credentials> tls_credentials,
                      RefCountedPtr<grpc_call_credentials> alts_credentials);

  ~DualCallCredentials() override;

  void Orphaned() override;

  static UniqueTypeName Type();

  UniqueTypeName type() const override { return Type(); }

  ArenaPromise<absl::StatusOr<ClientMetadataHandle>> GetRequestMetadata(
      ClientMetadataHandle initial_metadata, const GetRequestMetadataArgs* args) override;

 private:
  int cmp_impl(const grpc_call_credentials* other) const override {
    return QsortCompare(static_cast<const grpc_call_credentials*>(this), other);
  }
  std::string debug_string() override;

  RefCountedPtr<grpc_call_credentials> tls_credentials_;
  RefCountedPtr<grpc_call_credentials> alts_credentials_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CREDENTIALS_CALL_DUAL_DUAL_CALL_CREDENTIALS_H
