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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CONNECTION_ID_GENERATOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CONNECTION_ID_GENERATOR_H

#include <map>

#include "absl/strings/string_view.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

// Generates somewhat human-readable unique identifiers from package name and
// class name. We will generate a Id that only contains unreserved URI
// characters (uppercase and lowercase letters, decimal digits, hyphen, period,
// underscore, and tilde).
class ConnectionIdGenerator {
 public:
  std::string Generate(absl::string_view uri);

 private:
  // Our generated Id need to fit in unix socket path length limit. We use 100
  // here to be safe.
  const size_t kPathLengthLimit = 100;

  grpc_core::Mutex m_;
  // Every generated identifier will followed by the value of this counter to
  // make sure every generated id is unique.
  int count_ ABSL_GUARDED_BY(m_);
};

ConnectionIdGenerator* GetConnectionIdGenerator();

}  // namespace grpc_binder

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CONNECTION_ID_GENERATOR_H
