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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CONNECTION_ID_GENERATOR_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CONNECTION_ID_GENERATOR_H

#include <map>

#include "absl/strings/string_view.h"

#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

// Generates somewhat human-readable unique identifiers from package name and
// class name
class ConnectionIdGenerator {
 public:
  std::string Generate(absl::string_view package_name,
                       absl::string_view class_name);

 private:
  const size_t kPrefixLengthLimit = 100;

  grpc_core::Mutex m_;
  std::map<std::string, int> prefix_count_ ABSL_GUARDED_BY(m_);
};

ConnectionIdGenerator* GetConnectionIdGenerator();

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CONNECTION_ID_GENERATOR_H
