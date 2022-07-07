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
#include <memory>

#include <grpc/grpc_security.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/security/auth_context.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/cpp/common/secure_auth_context.h"

namespace grpc {

std::shared_ptr<const AuthContext> CreateAuthContext(grpc_call* call) {
  if (call == nullptr) {
    return std::shared_ptr<const AuthContext>();
  }
  grpc_core::RefCountedPtr<grpc_auth_context> ctx(grpc_call_auth_context(call));
  return std::make_shared<SecureAuthContext>(ctx.get());
}

}  // namespace grpc
