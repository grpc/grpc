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

#include <grpc/support/port_platform.h>

#include "src/core/lib/resolver/server_address.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/resolver/resolver_attributes.h"

namespace grpc_core {

//
// ServerAddress
//

ServerAddress::ServerAddress(const grpc_resolved_address& address,
                             grpc_channel_args* args,
                             ResolverAttributeMap subchannel_attributes,
                             ResolverAttributeMap lb_policy_attributes)
    : address_(address),
      args_(args),
      subchannel_attributes_(std::move(subchannel_attributes)),
      lb_policy_attributes_(std::move(lb_policy_attributes)) {}

ServerAddress::ServerAddress(const ServerAddress& other)
    : address_(other.address_),
      args_(grpc_channel_args_copy(other.args_)),
      subchannel_attributes_(other.subchannel_attributes_),
      lb_policy_attributes_(other.lb_policy_attributes_) {}

ServerAddress& ServerAddress::operator=(const ServerAddress& other) {
  if (&other != this) {
    address_ = other.address_;
    grpc_channel_args_destroy(args_);
    args_ = grpc_channel_args_copy(other.args_);
    subchannel_attributes_ = other.subchannel_attributes_;
    lb_policy_attributes_ = other.lb_policy_attributes_;
  }
  return *this;
}

ServerAddress::ServerAddress(ServerAddress&& other) noexcept
    : address_(other.address_),
      args_(absl::exchange(other.args_, nullptr)),
      subchannel_attributes_(std::move(other.subchannel_attributes_)),
      lb_policy_attributes_(std::move(other.lb_policy_attributes_)) {}

ServerAddress& ServerAddress::operator=(ServerAddress&& other) noexcept {
  address_ = other.address_;
  grpc_channel_args_destroy(args_);
  args_ = absl::exchange(other.args_, nullptr);
  subchannel_attributes_ = std::move(other.subchannel_attributes_);
  lb_policy_attributes_ = std::move(other.lb_policy_attributes_);
  return *this;
}

int ServerAddress::Compare(const ServerAddress& other) const {
  if (address_.len > other.address_.len) return 1;
  if (address_.len < other.address_.len) return -1;
  int retval = memcmp(address_.addr, other.address_.addr, address_.len);
  if (retval != 0) return retval;
  retval = grpc_channel_args_compare(args_, other.args_);
  if (retval != 0) return retval;
  retval = subchannel_attributes_.Compare(other.subchannel_attributes_);
  if (retval != 0) return retval;
  return lb_policy_attributes_.Compare(other.lb_policy_attributes_);
}

std::string ServerAddress::ToString() const {
  auto addr_str = grpc_sockaddr_to_string(&address_, false);
  std::vector<std::string> parts = {
      addr_str.ok() ? addr_str.value() : addr_str.status().ToString(),
  };
  if (args_ != nullptr) {
    parts.emplace_back(absl::StrCat("args=", grpc_channel_args_string(args_)));
  }
  if (!subchannel_attributes_.empty()) {
    parts.push_back(absl::StrCat("subchannel_attributes=",
                                 subchannel_attributes_.ToString()));
  }
  if (!lb_policy_attributes_.empty()) {
    parts.push_back(absl::StrCat("lb_policy_attributes=",
                                 lb_policy_attributes_.ToString()));
  }
  return absl::StrJoin(parts, " ");
}

//
// ServerAddressWeightAttribute
//

UniqueTypeName ServerAddressWeightAttribute::Type() {
  static auto* kFactory = new UniqueTypeName::Factory("server_address_weight");
  return kFactory->Create();
}

std::string ServerAddressWeightAttribute::ToString() const {
  return absl::StrFormat("%d", weight_);
}

}  // namespace grpc_core
