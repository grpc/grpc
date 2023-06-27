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

#include "src/core/lib/resolver/endpoint_addresses.h"

#include <string.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"

// IWYU pragma: no_include <sys/socket.h>

namespace grpc_core {

EndpointAddresses::EndpointAddresses(const grpc_resolved_address& address,
                                     const ChannelArgs& args)
    : addresses_(1, address), args_(args) {}

EndpointAddresses::EndpointAddresses(
    std::vector<grpc_resolved_address> addresses, const ChannelArgs& args)
    : addresses_(std::move(addresses)), args_(args) {}

EndpointAddresses::EndpointAddresses(const EndpointAddresses& other)
    : addresses_(other.addresses_), args_(other.args_) {}

EndpointAddresses& EndpointAddresses::operator=(
    const EndpointAddresses& other) {
  if (&other == this) return *this;
  addresses_ = other.addresses_;
  args_ = other.args_;
  return *this;
}

EndpointAddresses::EndpointAddresses(EndpointAddresses&& other) noexcept
    : addresses_(std::move(other.addresses_)), args_(std::move(other.args_)) {}

EndpointAddresses& EndpointAddresses::operator=(
    EndpointAddresses&& other) noexcept {
  addresses_ = std::move(other.addresses_);
  args_ = std::move(other.args_);
  return *this;
}

int EndpointAddresses::Cmp(const EndpointAddresses& other) const {
  for (size_t i = 0; i < addresses_.size(); ++i) {
    if (other.addresses_.size() == i) return 1;
    if (addresses_[i].len > other.addresses_[i].len) return 1;
    if (addresses_[i].len < other.addresses_[i].len) return -1;
    int retval =
        memcmp(addresses_[i].addr, other.addresses_[i].addr, addresses_[i].len);
    if (retval != 0) return retval;
  }
  if (other.addresses_.size() > addresses_.size()) return -1;
  return QsortCompare(args_, other.args_);
}

std::string EndpointAddresses::ToString() const {
  std::vector<std::string> addr_strings;
  for (const auto& address : addresses_) {
    auto addr_str = grpc_sockaddr_to_string(&address, false);
    addr_strings.push_back(
        addr_str.ok() ? std::move(*addr_str) : addr_str.status().ToString());
  }
  std::vector<std::string> parts = {
      absl::StrCat("addrs=[", absl::StrJoin(addr_strings, ", "), "]")
  };
  if (args_ != ChannelArgs()) {
    parts.emplace_back(absl::StrCat("args=", args_.ToString()));
  }
  return absl::StrJoin(parts, " ");
}

}  // namespace grpc_core
