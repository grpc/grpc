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

#ifndef GRPCPP_SECURITY_BINDER_SECURITY_POLICY_H
#define GRPCPP_SECURITY_BINDER_SECURITY_POLICY_H

#include <memory>

namespace grpc {
namespace experimental {
namespace binder {

// EXPERIMENTAL Determinines if a connection is allowed to be
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

// EXPERIMENTAL Allows all connection. Anything on the Android device will be
// able to connect, use with caution!
class UntrustedSecurityPolicy : public SecurityPolicy {
 public:
  UntrustedSecurityPolicy();
  ~UntrustedSecurityPolicy() override;
  bool IsAuthorized(int uid) override;
};

// EXPERIMENTAL Only allows the connections from processes with the same UID. In
// most cases this means "from the same APK".
class InternalOnlySecurityPolicy : public SecurityPolicy {
 public:
  InternalOnlySecurityPolicy();
  ~InternalOnlySecurityPolicy() override;
  bool IsAuthorized(int uid) override;
};

}  // namespace binder
}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_BINDER_SECURITY_POLICY_H
