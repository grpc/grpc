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

#include <grpc/support/port_platform.h>

#include "src/core/lib/resolver/server_address.h"

#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"

// IWYU pragma: no_include <sys/socket.h>

namespace grpc_core {

//
// ServerAddress
//

ServerAddress::ServerAddress(const grpc_resolved_address& address,
                             const ChannelArgs& args)
    : address_(address), args_(args) {}

ServerAddress::ServerAddress(const ServerAddress& other)
    : address_(other.address_), args_(other.args_) {}

ServerAddress& ServerAddress::operator=(const ServerAddress& other) {
  if (&other == this) return *this;
  address_ = other.address_;
  args_ = other.args_;
  return *this;
}

ServerAddress::ServerAddress(ServerAddress&& other) noexcept
    : address_(other.address_), args_(std::move(other.args_)) {}

ServerAddress& ServerAddress::operator=(ServerAddress&& other) noexcept {
  address_ = other.address_;
  args_ = std::move(other.args_);
  return *this;
}

int ServerAddress::Cmp(const ServerAddress& other) const {
  if (address_.len > other.address_.len) return 1;
  if (address_.len < other.address_.len) return -1;
  int retval = memcmp(address_.addr, other.address_.addr, address_.len);
  if (retval != 0) return retval;
  return QsortCompare(args_, other.args_);
}

std::string ServerAddress::ToString() const {
  auto addr_str = grpc_sockaddr_to_string(&address_, false);
  std::vector<std::string> parts = {
      addr_str.ok() ? addr_str.value() : addr_str.status().ToString(),
  };
  if (args_ != ChannelArgs()) {
    parts.emplace_back(absl::StrCat("args=", args_.ToString()));
  }
  return absl::StrJoin(parts, " ");
}

}  // namespace grpc_core
