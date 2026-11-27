//
// Copyright 2026 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_RESOLVER_GOOGLE_C2P_GOOGLE_C2P_RESOLVER_H
#define GRPC_SRC_CORE_RESOLVER_GOOGLE_C2P_GOOGLE_C2P_RESOLVER_H

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Inform the gRPC library of the universe domain in which the process is
// running (for example, "googleapis.com"). This setting is used by the
// "google-c2p" resolver to construct the xDS server URI.
absl::Status SetGoogleC2PUniverseDomain(absl::string_view universe_domain);

// Returns the currently configured universe domain, or std::nullopt if not yet
// configured. Exposed for testing.
std::optional<std::string> GetGoogleC2PUniverseDomainForTesting();

// Returns the Traffic Director server URI from the cached google-c2p xDS
// bootstrap, or std::nullopt if bootstrap has not yet been constructed.
// Exposed for testing.
std::optional<std::string> GetGoogleC2PBootstrapServerUriForTesting();

// Resets global google-c2p resolver state (universe domain and cached
// bootstrap). Exposed for testing.
void ResetGoogleC2PResolverForTesting();

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_RESOLVER_GOOGLE_C2P_GOOGLE_C2P_RESOLVER_H
