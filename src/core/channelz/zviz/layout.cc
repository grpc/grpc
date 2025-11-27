// Copyright 2025 The gRPC Authors
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

#include "src/core/channelz/zviz/layout.h"

namespace grpc_zviz::layout {

Element& Element::AppendTimestamp(google::protobuf::Timestamp timestamp) {
  return AppendText(
      Intent::kTimestamp,
      absl::FormatTime(absl::FromUnixSeconds(timestamp.seconds()) +
                       absl::Nanoseconds(timestamp.nanos())));
}

Element& Element::AppendDuration(google::protobuf::Duration duration) {
  return AppendText(Intent::kDuration,
                    absl::FormatDuration(absl::Seconds(duration.seconds()) +
                                         absl::Nanoseconds(duration.nanos())));
}

Element& Element::AppendEntityLink(Environment& env, int64_t entity_id) {
  return AppendLink(Intent::kEntityRef, env.EntityLinkText(entity_id),
                    env.EntityLinkTarget(entity_id));
}

}  // namespace grpc_zviz::layout
