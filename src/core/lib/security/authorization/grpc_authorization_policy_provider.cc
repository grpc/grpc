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

#include "src/core/lib/security/authorization/grpc_authorization_policy_provider.h"

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <stdint.h>

#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/authorization/grpc_authorization_engine.h"
#include "src/core/lib/security/authorization/rbac_policy.h"
#include "src/core/lib/security/authorization/rbac_translator.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/load_file.h"
#include "src/core/util/status_helper.h"

namespace grpc_core {

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
      deny_engine_(policies.deny_policy.has_value()
                       ? MakeRefCounted<GrpcAuthorizationEngine>(
                             std::move(*policies.deny_policy))
                       : nullptr) {}

namespace {

absl::StatusOr<std::string> ReadPolicyFromFile(absl::string_view policy_path) {
  auto policy_slice =
      LoadFile(std::string(policy_path), /*add_null_terminator=*/false);
  if (!policy_slice.ok()) {
    return absl::InvalidArgumentError(policy_slice.status().ToString());
  }
  return std::string(policy_slice->as_string_view());
}

gpr_timespec TimeoutSecondsToDeadline(int64_t seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

}  // namespace

absl::StatusOr<RefCountedPtr<grpc_authorization_policy_provider>>
FileWatcherAuthorizationPolicyProvider::Create(
    absl::string_view authz_policy_path, unsigned int refresh_interval_sec) {
  CHECK(!authz_policy_path.empty());
  CHECK_GT(refresh_interval_sec, 0u);
  absl::Status status;
  auto provider = MakeRefCounted<FileWatcherAuthorizationPolicyProvider>(
      authz_policy_path, refresh_interval_sec, &status);
  if (!status.ok()) return status;
  return provider;
}

FileWatcherAuthorizationPolicyProvider::FileWatcherAuthorizationPolicyProvider(
    absl::string_view authz_policy_path, unsigned int refresh_interval_sec,
    absl::Status* status)
    : authz_policy_path_(std::string(authz_policy_path)),
      refresh_interval_sec_(refresh_interval_sec) {
  gpr_event_init(&shutdown_event_);
  // Initial read is done synchronously.
  *status = ForceUpdate();
  if (!status->ok()) {
    return;
  }
  auto thread_lambda = [](void* arg) {
    WeakRefCountedPtr<FileWatcherAuthorizationPolicyProvider> provider(
        static_cast<FileWatcherAuthorizationPolicyProvider*>(arg));
    CHECK(provider != nullptr);
    while (true) {
      void* value = gpr_event_wait(
          &provider->shutdown_event_,
          TimeoutSecondsToDeadline(provider->refresh_interval_sec_));
      if (value != nullptr) {
        return;
      }
      absl::Status status = provider->ForceUpdate();
      if (GRPC_TRACE_FLAG_ENABLED(grpc_authz_api) && !status.ok()) {
        LOG(ERROR) << "authorization policy reload status. code="
                   << static_cast<int>(status.code())
                   << " error_details=" << status.message();
      }
    }
  };
  refresh_thread_ = std::make_unique<Thread>(
      "FileWatcherAuthorizationPolicyProvider_refreshing_thread", thread_lambda,
      WeakRef().release());
  refresh_thread_->Start();
}

void FileWatcherAuthorizationPolicyProvider::SetCallbackForTesting(
    std::function<void(bool contents_changed, absl::Status status)> cb) {
  MutexLock lock(&mu_);
  cb_ = std::move(cb);
}

absl::Status FileWatcherAuthorizationPolicyProvider::ForceUpdate() {
  bool contents_changed = false;
  auto done_early = [&](absl::Status status) {
    MutexLock lock(&mu_);
    if (cb_ != nullptr) {
      cb_(contents_changed, status);
    }
    return status;
  };
  absl::StatusOr<std::string> file_contents =
      ReadPolicyFromFile(authz_policy_path_);
  if (!file_contents.ok()) {
    return done_early(file_contents.status());
  }
  if (file_contents_ == *file_contents) {
    return done_early(absl::OkStatus());
  }
  file_contents_ = std::move(*file_contents);
  contents_changed = true;
  auto rbac_policies_or = GenerateRbacPolicies(file_contents_);
  if (!rbac_policies_or.ok()) {
    return done_early(rbac_policies_or.status());
  }
  MutexLock lock(&mu_);
  allow_engine_ = MakeRefCounted<GrpcAuthorizationEngine>(
      std::move(rbac_policies_or->allow_policy));
  if (rbac_policies_or->deny_policy.has_value()) {
    deny_engine_ = MakeRefCounted<GrpcAuthorizationEngine>(
        std::move(*rbac_policies_or->deny_policy));
  } else {
    deny_engine_.reset();
  }
  if (cb_ != nullptr) {
    cb_(contents_changed, absl::OkStatus());
  }
  GRPC_TRACE_LOG(grpc_authz_api, INFO)
      << "authorization policy reload status: successfully loaded new "
         "policy\n"
      << file_contents_;
  return absl::OkStatus();
}

void FileWatcherAuthorizationPolicyProvider::Orphaned() {
  gpr_event_set(&shutdown_event_, reinterpret_cast<void*>(1));
  if (refresh_thread_ != nullptr) {
    refresh_thread_->Join();
  }
}

}  // namespace grpc_core

// Wrapper APIs declared in grpc_security.h

grpc_authorization_policy_provider*
grpc_authorization_policy_provider_static_data_create(
    const char* authz_policy, grpc_status_code* code,
    const char** error_details) {
  CHECK_NE(authz_policy, nullptr);
  auto provider_or =
      grpc_core::StaticDataAuthorizationPolicyProvider::Create(authz_policy);
  if (!provider_or.ok()) {
    *code = static_cast<grpc_status_code>(provider_or.status().code());
    *error_details =
        gpr_strdup(std::string(provider_or.status().message()).c_str());
    return nullptr;
  }
  return provider_or->release();
}

grpc_authorization_policy_provider*
grpc_authorization_policy_provider_file_watcher_create(
    const char* authz_policy_path, unsigned int refresh_interval_sec,
    grpc_status_code* code, const char** error_details) {
  CHECK_NE(authz_policy_path, nullptr);
  auto provider_or = grpc_core::FileWatcherAuthorizationPolicyProvider::Create(
      authz_policy_path, refresh_interval_sec);
  if (!provider_or.ok()) {
    *code = static_cast<grpc_status_code>(provider_or.status().code());
    *error_details =
        gpr_strdup(std::string(provider_or.status().message()).c_str());
    return nullptr;
  }
  return provider_or->release();
}

void grpc_authorization_policy_provider_release(
    grpc_authorization_policy_provider* provider) {
  if (provider != nullptr) provider->Unref();
}
