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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"

// The subchannel pool to reuse subchannels.
#define GRPC_ARG_SUBCHANNEL_POOL "grpc.internal.subchannel_pool"
// The subchannel key ID that is only used in test to make each key unique.
#define GRPC_ARG_SUBCHANNEL_KEY_TEST_ONLY_ID "grpc.subchannel_key_test_only_id"

namespace grpc_core {

SubchannelKey::SubchannelKey(const grpc_resolved_address& address,
                             const ChannelArgs& args)
    : address_(address), args_(args) {}

int SubchannelKey::Compare(const SubchannelKey& other) const {
  if (address_.len < other.address_.len) return -1;
  if (address_.len > other.address_.len) return 1;
  int r = memcmp(address_.addr, other.address_.addr, address_.len);
  if (r != 0) return r;
  return QsortCompare(args_, other.args_);
}

std::string SubchannelKey::ToString() const {
  auto addr_uri = grpc_sockaddr_to_uri(&address_);
  return absl::StrCat(
      "{address=",
      addr_uri.ok() ? addr_uri.value() : addr_uri.status().ToString(),
      ", args=", args_.ToString(), "}");
}

absl::string_view SubchannelPoolInterface::ChannelArgName() {
  return GRPC_ARG_SUBCHANNEL_POOL;
}

}  // namespace grpc_core
