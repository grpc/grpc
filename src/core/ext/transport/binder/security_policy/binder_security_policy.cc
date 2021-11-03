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

#include <grpc/support/port_platform.h>

#ifndef GRPC_NO_BINDER

#include <grpcpp/security/binder_security_policy.h>

#ifdef GPR_ANDROID

#include <unistd.h>

#endif

namespace grpc {
namespace experimental {
namespace binder {

UntrustedSecurityPolicy::UntrustedSecurityPolicy() = default;

UntrustedSecurityPolicy::~UntrustedSecurityPolicy() = default;

bool UntrustedSecurityPolicy::IsAuthorized(int) { return true; };

InternalOnlySecurityPolicy::InternalOnlySecurityPolicy() = default;

InternalOnlySecurityPolicy::~InternalOnlySecurityPolicy() = default;

#ifdef GPR_ANDROID
bool InternalOnlySecurityPolicy::IsAuthorized(int uid) {
  return static_cast<uid_t>(uid) == getuid();
}
#else
bool InternalOnlySecurityPolicy::IsAuthorized(int) { return false; }
#endif

}  // namespace binder
}  // namespace experimental
}  // namespace grpc
#endif
