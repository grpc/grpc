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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_AUTHORIZATION_POLICY_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_AUTHORIZATION_POLICY_PROVIDER_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/status/statusor.h"

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/security/authorization/authorization_policy_provider.h"
#include "src/core/lib/security/authorization/rbac_translator.h"

namespace grpc_core {

// Provider class will get SDK Authorization policy from string during
// initialization. This policy will be translated to Envoy RBAC policies and
// used to initialize allow and deny AuthorizationEngine objects. This provider
// will return the same authorization engines everytime.
class StaticDataAuthorizationPolicyProvider
    : public grpc_authorization_policy_provider {
 public:
  static absl::StatusOr<RefCountedPtr<grpc_authorization_policy_provider>>
  Create(absl::string_view authz_policy);

  // Use factory method "Create" to create an instance of
  // StaticDataAuthorizationPolicyProvider.
  explicit StaticDataAuthorizationPolicyProvider(RbacPolicies policies);

  AuthorizationEngines engines() override {
    return {allow_engine_, deny_engine_};
  }

  void Orphan() override {}

 private:
  RefCountedPtr<AuthorizationEngine> allow_engine_;
  RefCountedPtr<AuthorizationEngine> deny_engine_;
};

// Provider class will get SDK Authorization policy from provided file path.
// This policy will be translated to Envoy RBAC policies and used to initialize
// allow and deny AuthorizationEngine objects. This provider will periodically
// load file contents in specified path, and upon modification update the engine
// instances with new policy configuration. During reload if the file contents
// are invalid or there are I/O errors, we will skip that particular update and
// return the error status back to user via callback. The authorization
// decisions will be made using the latest valid policy.
class FileWatcherAuthorizationPolicyProvider
    : public grpc_authorization_policy_provider {
 public:
  static absl::StatusOr<RefCountedPtr<grpc_authorization_policy_provider>>
  Create(
      absl::string_view authz_policy_path, unsigned int refresh_interval_sec,
      std::function<void(grpc_status_code code, const char* error_details)> cb);

  // Use factory method "Create" to create an instance of
  // FileWatcherAuthorizationPolicyProvider.
  FileWatcherAuthorizationPolicyProvider(
      absl::string_view authz_policy_path, unsigned int refresh_interval_sec,
      std::function<void(grpc_status_code code, const char* error_details)> cb,
      absl::Status* status);

  ~FileWatcherAuthorizationPolicyProvider() override;

  void Orphan() override;

  AuthorizationEngines engines() override {
    grpc_core::MutexLock lock(&mu_);
    return {allow_engine_, deny_engine_};
  }

 private:
  // Force an update from the file system regardless of the interval.
  absl::Status ForceUpdate();

  std::string authz_policy_path_;
  unsigned int refresh_interval_sec_;

  Thread* refresh_thread_ = nullptr;
  gpr_event shutdown_event_;

  // Tracks reload failures. Useful to determine when provider recovers from
  // failure.
  bool reload_failed_ = false;
  // Callback function to execute when we get an error during policy reload. The
  // callback is also executed when we recover from error.
  std::function<void(grpc_status_code code, const char* error_details)> cb_;

  grpc_core::Mutex mu_;
  // The latest valid policy.
  std::string authz_policy_ ABSL_GUARDED_BY(mu_);
  // Engines created using authz_policy_.
  RefCountedPtr<AuthorizationEngine> allow_engine_ ABSL_GUARDED_BY(mu_);
  RefCountedPtr<AuthorizationEngine> deny_engine_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_GRPC_AUTHORIZATION_POLICY_PROVIDER_H
