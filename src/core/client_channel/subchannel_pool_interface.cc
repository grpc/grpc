//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/client_channel/subchannel_pool_interface.h"

#include <grpc/support/port_platform.h>
#include <string.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

// The subchannel pool to reuse subchannels.
#define GRPC_ARG_SUBCHANNEL_POOL "grpc.internal.subchannel_pool"
// The subchannel key ID that is only used in test to make each key unique.
#define GRPC_ARG_SUBCHANNEL_KEY_TEST_ONLY_ID "grpc.subchannel_key_test_only_id"

namespace grpc_core {

SubchannelKey::SubchannelKey(std::string address, const ChannelArgs& args)
    : address_(std::move(address)), args_(args) {}

int SubchannelKey::Compare(const SubchannelKey& other) const {
  int r = address_.compare(other.address_);
  if (r != 0) return r;
  return QsortCompare(args_, other.args_);
}

std::string SubchannelKey::ToString() const {
  return absl::StrCat("{address=", address_, ", args=", args_.ToString(), "}");
}

absl::string_view SubchannelPoolInterface::ChannelArgName() {
  return GRPC_ARG_SUBCHANNEL_POOL;
}

}  // namespace grpc_core
