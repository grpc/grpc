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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_SECURITY_POLICY_SECURITY_POLICY_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_SECURITY_POLICY_SECURITY_POLICY_H

#include <grpc/support/port_platform.h>

namespace grpc {
namespace experimental {
namespace binder {

// This interface is for determining if a connection is allowed to be
// established on Android. See https://source.android.com/security/app-sandbox
// for more info about UID.
class SecurityPolicy {
 public:
  virtual ~SecurityPolicy() = default;
  // Returns true if the UID is authorized to connect.
  // Must return the same value for the same inputs so callers can safely cache
  // the result.
  virtual bool IsAuthorized(int uid) = 0;
};

}  // namespace binder
}  // namespace experimental
}  // namespace grpc

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_SECURITY_POLICY_SECURITY_POLICY_H
