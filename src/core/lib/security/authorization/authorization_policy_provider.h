// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_POLICY_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_POLICY_PROVIDER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/security/authorization/authorization_engine.h"

struct grpc_authorization_policy_provider
    : public grpc_core::DualRefCounted<grpc_authorization_policy_provider> {
 public:
  virtual grpc_core::RefCountedPtr<grpc_core::AuthorizationEngine>
  allow_engine() const = 0;
  virtual grpc_core::RefCountedPtr<grpc_core::AuthorizationEngine> deny_engine()
      const = 0;
};

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_POLICY_PROVIDER_H
