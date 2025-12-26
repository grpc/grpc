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

#include "src/core/resolver/endpoint_addresses.h"

#include <grpc/support/port_platform.h>
#include <string.h>

#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/useful.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

// IWYU pragma: no_include <sys/socket.h>

namespace grpc_core {

EndpointAddresses::EndpointAddresses(const std::string& address,
                                     const ChannelArgs& args)
    : addresses_(1, address), args_(args) {}

EndpointAddresses::EndpointAddresses(
    std::vector<std::string> addresses, const ChannelArgs& args)
    : addresses_(std::move(addresses)), args_(args) {
  GRPC_CHECK(!addresses_.empty());
}

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
    int retval = addresses_[i].compare(other.addresses_[i]);
    if (retval != 0) return retval;
  }
  if (other.addresses_.size() > addresses_.size()) return -1;
  return QsortCompare(args_, other.args_);
}

std::string EndpointAddresses::ToString() const {
  std::vector<std::string> parts = {
      absl::StrCat("addrs=[", absl::StrJoin(addresses_, ", "), "]")};
  if (args_ != ChannelArgs()) {
    parts.emplace_back(absl::StrCat("args=", args_.ToString()));
  }
  return absl::StrJoin(parts, " ");
}

bool StringLessThan::operator()(
    const std::string& str1,
    const std::string& str2) const {
  return str1 < str2;
}

bool EndpointAddressSet::operator==(const EndpointAddressSet& other) const {
  if (addresses_.size() != other.addresses_.size()) return false;
  auto other_it = other.addresses_.begin();
  for (auto it = addresses_.begin(); it != addresses_.end(); ++it) {
    GRPC_CHECK(other_it != other.addresses_.end());
    if (*it != *other_it) {
      return false;
    }
    ++other_it;
  }
  return true;
}

bool EndpointAddressSet::operator<(const EndpointAddressSet& other) const {
  auto other_it = other.addresses_.begin();
  for (auto it = addresses_.begin(); it != addresses_.end(); ++it) {
    if (other_it == other.addresses_.end()) return false;
    int r = it->compare(*other_it);
    if (r != 0) return r < 0;
    ++other_it;
  }
  return other_it != other.addresses_.end();
}

std::string EndpointAddressSet::ToString() const {
  return absl::StrCat("{", absl::StrJoin(addresses_, ", "), "}");
}

}  // namespace grpc_core
