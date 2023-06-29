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

#ifndef GRPC_SRC_CORE_LIB_RESOLVER_ENDPOINT_ADDRESSES_H
#define GRPC_SRC_CORE_LIB_RESOLVER_ENDPOINT_ADDRESSES_H

#include <grpc/support/port_platform.h>

#include <set>
#include <string>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolved_address.h"

// A channel arg key prefix used for args that are intended to be used
// only internally to resolvers and LB policies and should not be part
// of the subchannel key.  The channel will automatically filter out any
// args with this prefix from the subchannel's args.
#define GRPC_ARG_NO_SUBCHANNEL_PREFIX "grpc.internal.no_subchannel."

// A channel arg indicating the weight of an address.
#define GRPC_ARG_ADDRESS_WEIGHT GRPC_ARG_NO_SUBCHANNEL_PREFIX "address.weight"

namespace grpc_core {

// A list of addresses for a given endpoint with an associated set of channel
// args.  Any args present here will be merged into the channel args when a
// subchannel is created for each address.
class EndpointAddresses {
 public:
  // For backward compatibility.
  // TODO(roth): Remove when callers have been updated.
  EndpointAddresses(const grpc_resolved_address& address,
                    const ChannelArgs& args);

  // addresses must not be empty.
  EndpointAddresses(std::vector<grpc_resolved_address> addresses,
                    const ChannelArgs& args);

  // Copyable.
  EndpointAddresses(const EndpointAddresses& other);
  EndpointAddresses& operator=(const EndpointAddresses& other);

  // Movable.
  EndpointAddresses(EndpointAddresses&& other) noexcept;
  EndpointAddresses& operator=(EndpointAddresses&& other) noexcept;

  bool operator==(const EndpointAddresses& other) const {
    return Cmp(other) == 0;
  }
  bool operator<(const EndpointAddresses& other) const {
    return Cmp(other) < 0;
  }

  int Cmp(const EndpointAddresses& other) const;

  // For backward compatibility only.
  // TODO(roth): Remove when all callers have been updated.
  const grpc_resolved_address& address() const { return addresses_[0]; }

  const std::vector<grpc_resolved_address>& addresses() const {
    return addresses_;
  }
  const ChannelArgs& args() const { return args_; }

  // TODO(ctiller): Prior to making this a public API we should ensure that the
  // channel args are not part of the generated string, lest we make that debug
  // format load-bearing via Hyrum's law.
  std::string ToString() const;

 private:
  std::vector<grpc_resolved_address> addresses_;
  ChannelArgs args_;
};

using EndpointAddressesList = std::vector<EndpointAddresses>;

class EndpointAddressSet {
 public:
  explicit EndpointAddressSet(
      const std::vector<grpc_resolved_address>& addresses)
      : addresses_(addresses.begin(), addresses.end()) {}

  bool operator==(const EndpointAddressSet& other) const;
  bool operator<(const EndpointAddressSet& other) const;

 private:
  struct ResolvedAddressLessThan {
    bool operator()(const grpc_resolved_address& addr1,
                    const grpc_resolved_address& addr2) const;
  };

  std::set<grpc_resolved_address, ResolvedAddressLessThan> addresses_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_RESOLVER_ENDPOINT_ADDRESSES_H
