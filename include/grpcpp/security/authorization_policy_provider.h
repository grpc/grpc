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

#ifndef GRPCPP_SECURITY_AUTHORIZATION_POLICY_PROVIDER_H
#define GRPCPP_SECURITY_AUTHORIZATION_POLICY_PROVIDER_H

#include <grpc/status.h>
#include <grpcpp/impl/codegen/grpc_library.h>

#include <memory>

// TODO(yihuazhang): remove the forward declaration here and include
// <grpc/grpc_security.h> directly once the insecure builds are cleaned up.
typedef struct grpc_authorization_policy_provider
    grpc_authorization_policy_provider;

namespace grpc {
namespace experimental {

// Wrapper around C-core grpc_authorization_policy_provider. Internally, it
// handles creating and updating authorization engine objects, using SDK
// authorization policy.
class AuthorizationPolicyProviderInterface {
 public:
  virtual ~AuthorizationPolicyProviderInterface() = default;
  virtual grpc_authorization_policy_provider* c_provider() = 0;
};

// Implementation obtains authorization policy from static string. This provider
// will always return the same authorization engines.
class StaticDataAuthorizationPolicyProvider
    : public AuthorizationPolicyProviderInterface {
 public:
  static std::shared_ptr<StaticDataAuthorizationPolicyProvider> Create(
      const std::string& authz_policy, grpc::Status* status);

  // Use factory method "Create" to create an instance of
  // StaticDataAuthorizationPolicyProvider.
  explicit StaticDataAuthorizationPolicyProvider(
      grpc_authorization_policy_provider* provider)
      : c_provider_(provider) {}

  ~StaticDataAuthorizationPolicyProvider() override;

  grpc_authorization_policy_provider* c_provider() override {
    return c_provider_;
  }

 private:
  grpc_authorization_policy_provider* c_provider_ = nullptr;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_AUTHORIZATION_POLICY_PROVIDER_H
