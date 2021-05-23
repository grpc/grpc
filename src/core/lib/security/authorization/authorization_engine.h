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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_ENGINE_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_ENGINE_H

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/security/authorization/evaluate_args.h"

namespace grpc_core {

// Interface for gRPC Authorization Engine.
class AuthorizationEngine : public RefCounted<AuthorizationEngine> {
 public:
  struct Decision {
    enum class Type {
      kAllow,
      kDeny,
    };
    Type type;
    std::string matching_policy_name;
  };

  virtual Decision Evaluate(const EvaluateArgs& args) const = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_AUTHORIZATION_ENGINE_H
