//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_TEST_CPP_UTIL_CHANNELZ_UTIL_H
#define GRPC_TEST_CPP_UTIL_CHANNELZ_UTIL_H

#include <vector>

#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "absl/strings/string_view.h"

namespace grpc {
namespace testing {

// A collection of channelz utilities that are useful in tests.
class ChannelzUtil {
 public:
  // Returns the channelz entities for all subchannels for the specified
  // address URI.
  static std::vector<grpc::channelz::v2::Entity> GetSubchannelsForAddress(
      absl::string_view address);

  // Returns the channelz entities for all connections for the specified
  // subchannel.
  static std::vector<grpc::channelz::v2::Entity> GetSubchannelConnections(
      int64_t subchannel_id);
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_CHANNELZ_UTIL_H
