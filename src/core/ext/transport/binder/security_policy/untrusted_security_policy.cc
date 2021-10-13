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

#include "src/core/ext/transport/binder/security_policy/untrusted_security_policy.h"

namespace grpc {
namespace experimental {
namespace binder {

UntrustedSecurityPolicy::UntrustedSecurityPolicy() = default;

UntrustedSecurityPolicy::~UntrustedSecurityPolicy() = default;

bool UntrustedSecurityPolicy::IsAuthorized(int) { return true; };

}  // namespace binder
}  // namespace experimental
}  // namespace grpc
