// Copyright 2024 The gRPC Authors.
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

#ifndef GRPC_SUPPORT_METRICS_H
#define GRPC_SUPPORT_METRICS_H

#include "absl/strings/string_view.h"

#include <grpc/support/port_platform.h>

namespace grpc_core {
namespace experimental {

// Configuration (scope) for a specific client channel to be used for stats
// plugins.
class StatsPluginChannelScope {
 public:
  StatsPluginChannelScope(absl::string_view target,
                          absl::string_view default_authority)
      : target_(target), default_authority_(default_authority) {}

  /// Returns the target used for creating the channel in the canonical form.
  /// (Canonicalized target definition -
  /// https://github.com/grpc/proposal/blob/master/A66-otel-stats.md)
  absl::string_view target() const { return target_; }
  /// Returns the default authority for the channel.
  absl::string_view default_authority() const { return default_authority_; }

 private:
  // Disable copy constructor and copy-assignment operator.
  StatsPluginChannelScope(const StatsPluginChannelScope&) = delete;
  StatsPluginChannelScope& operator=(const StatsPluginChannelScope&) = delete;
  absl::string_view target_;
  absl::string_view default_authority_;
};

}  // namespace experimental
}  // namespace grpc_core

#endif /* GRPC_SUPPORT_METRICS_H */
