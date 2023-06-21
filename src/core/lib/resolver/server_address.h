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

#ifndef GRPC_SRC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H
#define GRPC_SRC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/resolved_address.h"

// A channel arg key prefix used for args that are intended to be used
// only internally to resolvers and LB policies and should not be part
// of the subchannel key.  The channel will automatically filter out any
// args with this prefix from the subchannel's args.
#define GRPC_ARG_NO_SUBCHANNEL_PREFIX "grpc.internal.no_subchannel."

// A channel arg indicating the weight of an address.
#define GRPC_ARG_ADDRESS_WEIGHT GRPC_ARG_NO_SUBCHANNEL_PREFIX "address.weight"

namespace grpc_core {

//
// ServerAddress
//

// A server address is a grpc_resolved_address with an associated set of
// channel args.  Any args present here will be merged into the channel
// args when a subchannel is created for this address.
class ServerAddress {
 public:
  // Base class for resolver-supplied attributes.
  // Unlike channel args, these attributes don't affect subchannel
  // uniqueness or behavior.  They are for use by LB policies only.
  //
  // Attributes are keyed by a C string that is unique by address, not
  // by value.  All attributes added with the same key must be of the
  // same type.
  class AttributeInterface {
   public:
    virtual ~AttributeInterface() = default;

    // Creates a copy of the attribute.
    virtual std::unique_ptr<AttributeInterface> Copy() const = 0;

    // Compares this attribute with another.
    virtual int Cmp(const AttributeInterface* other) const = 0;

    // Returns a human-readable representation of the attribute.
    virtual std::string ToString() const = 0;
  };

  ServerAddress(const grpc_resolved_address& address, const ChannelArgs& args,
                std::map<const char*, std::unique_ptr<AttributeInterface>>
                    attributes = {});

  // Copyable.
  ServerAddress(const ServerAddress& other);
  ServerAddress& operator=(const ServerAddress& other);

  // Movable.
  ServerAddress(ServerAddress&& other) noexcept;
  ServerAddress& operator=(ServerAddress&& other) noexcept;

  bool operator==(const ServerAddress& other) const { return Cmp(other) == 0; }

  int Cmp(const ServerAddress& other) const;

  const grpc_resolved_address& address() const { return address_; }
  const ChannelArgs& args() const { return args_; }

  std::map<const char*, std::unique_ptr<AttributeInterface>> attributes()
      const {
    std::map<const char*, std::unique_ptr<AttributeInterface>> attributes;
    for (const auto& p : attributes_) {
      attributes[p.first] = p.second->Copy();
    }
    return attributes;
  }

  const AttributeInterface* GetAttribute(const char* key) const;

  // Returns a copy of the address with a modified attribute.
  // If the new value is null, the attribute is removed.
  ServerAddress WithAttribute(const char* key,
                              std::unique_ptr<AttributeInterface> value) const;

  // TODO(ctiller): Prior to making this a public API we should ensure that the
  // channel args are not part of the generated string, lest we make that debug
  // format load-bearing via Hyrum's law.
  std::string ToString() const;

 private:
  grpc_resolved_address address_;
  ChannelArgs args_;
  std::map<const char*, std::unique_ptr<AttributeInterface>> attributes_;
};

//
// ServerAddressList
//

using ServerAddressList = std::vector<ServerAddress>;

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H
