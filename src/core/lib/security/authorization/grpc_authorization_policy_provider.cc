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

#include <grpc/grpc_security.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

namespace {

absl::StatusOr<std::string> ReadPolicyFromFile(absl::string_view policy_path) {
  grpc_slice policy_slice = grpc_empty_slice();
  grpc_error_handle error =
      grpc_load_file(std::string(policy_path).c_str(), 0, &policy_slice);
  if (error != GRPC_ERROR_NONE) {
    // TODO(ashithasantosh): grpc_error to absl::Status.
    absl::Status status = absl::Status(absl::StatusCode::kInvalidArgument,
                                       grpc_error_std_string(error));
    GRPC_ERROR_UNREF(error);
    return status;
  }
  std::string policy_contents(StringViewFromSlice(policy_slice));
  grpc_slice_unref_internal(policy_slice);
  return policy_contents;
}

gpr_timespec TimeoutSecondsToDeadline(int64_t seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

}  // namespace

//
// StaticData implementation
//

absl::StatusOr<RefCountedPtr<grpc_authorization_policy_provider>>
StaticDataAuthorizationPolicyProvider::Create(absl::string_view authz_policy) {
  auto policies_or = GenerateRbacPolicies(authz_policy);
  if (!policies_or.ok()) {
    return policies_or.status();
  }
  return MakeRefCounted<StaticDataAuthorizationPolicyProvider>(
      std::move(*policies_or));
}

StaticDataAuthorizationPolicyProvider::StaticDataAuthorizationPolicyProvider(
    RbacPolicies policies)
    : allow_engine_(MakeRefCounted<GrpcAuthorizationEngine>(
          std::move(policies.allow_policy))),
      deny_engine_(MakeRefCounted<GrpcAuthorizationEngine>(
          std::move(policies.deny_policy))) {}

//
// FileWatcher implementation
//

absl::StatusOr<RefCountedPtr<grpc_authorization_policy_provider>>
FileWatcherAuthorizationPolicyProvider::Create(
    absl::string_view authz_policy_path, unsigned int refresh_interval_sec) {
  GPR_ASSERT(!authz_policy_path.empty());
  GPR_ASSERT(refresh_interval_sec > 0);
  auto policy = ReadPolicyFromFile(authz_policy_path);
  if (!policy.ok()) {
    return policy.status();
  }
  if (policy->empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Authorization policy file is empty.");
  }
  auto policies_or = GenerateRbacPolicies(*policy);
  if (!policies_or.ok()) {
    return policies_or.status();
  }
  return MakeRefCounted<FileWatcherAuthorizationPolicyProvider>(
      authz_policy_path, refresh_interval_sec, std::move(*policy),
      std::move(*policies_or));
}

FileWatcherAuthorizationPolicyProvider::FileWatcherAuthorizationPolicyProvider(
    absl::string_view authz_policy_path, unsigned int refresh_interval_sec,
    std::string policy, RbacPolicies policies)
    : path_(authz_policy_path),
      refresh_interval_sec_(refresh_interval_sec),
      authz_policy_(policy) {
  gpr_event_init(&shutdown_event_);
  auto thread_lambda = [](void* arg) {
    FileWatcherAuthorizationPolicyProvider* provider =
        static_cast<FileWatcherAuthorizationPolicyProvider*>(arg);
    GPR_ASSERT(provider != nullptr);
    while (true) {
      void* value = gpr_event_wait(
          &provider->shutdown_event_,
          TimeoutSecondsToDeadline(provider->refresh_interval_sec_));
      if (value != nullptr) {
        return;
      }
      provider->ForceUpdate();
    }
  };
  refresh_thread_ = grpc_core::Thread(
      "FileWatcherAuthorizationPolicyProvider_refreshing_thread", thread_lambda,
      this);
  refresh_thread_.Start();
  allow_engine_ =
      MakeRefCounted<GrpcAuthorizationEngine>(std::move(policies.allow_policy));
  deny_engine_ =
      MakeRefCounted<GrpcAuthorizationEngine>(std::move(policies.deny_policy));
}

void FileWatcherAuthorizationPolicyProvider::ForceUpdate() {
  absl::StatusOr<std::string> policy = ReadPolicyFromFile(path_);
  if (!policy.ok() || policy->empty() || authz_policy_ == *policy) {
    return;
  }
  auto policies_or = GenerateRbacPolicies(*policy);
  if (!policies_or.ok()) {
    return;
  }
  authz_policy_ = *policy;
  // TODO(ashithasantosh): Handle references properly. Include locking.
  allow_engine_ = MakeRefCounted<GrpcAuthorizationEngine>(
      std::move(policies_or->allow_policy));
  deny_engine_ = MakeRefCounted<GrpcAuthorizationEngine>(
      std::move(policies_or->deny_policy));
}

FileWatcherAuthorizationPolicyProvider::
    ~FileWatcherAuthorizationPolicyProvider() {
  gpr_event_set(&shutdown_event_, reinterpret_cast<void*>(1));
  refresh_thread_.Join();
}

}  // namespace grpc_core

// Wrapper APIs declared in grpc_security.h

grpc_authorization_policy_provider*
grpc_authorization_policy_provider_static_data_create(
    const char* authz_policy, grpc_status_code* code,
    const char** error_details) {
  GPR_ASSERT(authz_policy != nullptr);
  auto provider_or =
      grpc_core::StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  if (!provider_or.ok()) {
    *code = static_cast<grpc_status_code>(provider_or.status().code());
    *error_details =
        gpr_strdup(std::string(provider_or.status().message()).c_str());
    return nullptr;
  }
  *code = GRPC_STATUS_OK;
  *error_details = nullptr;
  return provider_or->release();
}

void grpc_authorization_policy_provider_release(
    grpc_authorization_policy_provider* provider) {
  if (provider != nullptr) provider->Unref();
}
