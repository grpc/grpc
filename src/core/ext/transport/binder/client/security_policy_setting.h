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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_SECURITY_POLICY_SETTING_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_SECURITY_POLICY_SETTING_H

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/security/binder_security_policy.h>

#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

// A singleton class for setting security setting for each connection. This is
// required because we cannot pass security policy shared pointers around using
// gRPC arguments, we can only pass connection_id around as part of URI
class SecurityPolicySetting {
 public:
  void Set(absl::string_view connection_id,
           std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
               security_policy);
  std::shared_ptr<grpc::experimental::binder::SecurityPolicy> Get(
      absl::string_view connection_id);

 private:
  grpc_core::Mutex m_;
  absl::flat_hash_map<
      std::string, std::shared_ptr<grpc::experimental::binder::SecurityPolicy>>
      security_policy_map_ ABSL_GUARDED_BY(m_);
};

SecurityPolicySetting* GetSecurityPolicySetting();

}  // namespace grpc_binder

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_SECURITY_POLICY_SETTING_H
