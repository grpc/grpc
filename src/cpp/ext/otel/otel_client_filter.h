//
//
// Copyright 2023 gRPC authors.
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
//

#ifndef GRPC_SRC_CPP_EXT_OTEL_OTEL_CLIENT_FILTER_H
#define GRPC_SRC_CPP_EXT_OTEL_OTEL_CLIENT_FILTER_H

#include <grpc/support/port_platform.h>

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

class OpenTelemetryClientFilter : public grpc_core::ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<OpenTelemetryClientFilter> Create(
      const grpc_core::ChannelArgs& /*args*/,
      ChannelFilter::Args /*filter_args*/);

  grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> MakeCallPromise(
      grpc_core::CallArgs call_args,
      grpc_core::NextPromiseFactory next_promise_factory) override;

  absl::string_view filtered_target() const { return filtered_target_; }

  const ActivePluginOptionsView& active_plugin_options_view() const {
    return active_plugin_options_view_;
  }

 private:
  explicit OpenTelemetryClientFilter(std::string target);

  std::string filtered_target_;
  ActivePluginOptionsView active_plugin_options_view_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_OTEL_OTEL_CLIENT_FILTER_H
