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

#ifndef GRPC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H
#define GRPC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <vector>

#include "absl/container/inlined_vector.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/resolver/resolver_attributes.h"

namespace grpc_core {

//
// ServerAddress
//

// A server address is an address with two sets of attributes attached,
// one set that affects subchannel behavior, and another set that is
// visible only to LB policies.
// TODO(roth): Remove grpc_channel_args from this class.
class ServerAddress {
 public:
  ServerAddress(
      const grpc_resolved_address& address, grpc_channel_args* args,
      ResolverAttributeMap subchannel_attributes = ResolverAttributeMap(),
      ResolverAttributeMap lb_policy_attributes = ResolverAttributeMap());

  ~ServerAddress() { grpc_channel_args_destroy(args_); }

  // Copyable.
  ServerAddress(const ServerAddress& other);
  ServerAddress& operator=(const ServerAddress& other);

  // Movable.
  ServerAddress(ServerAddress&& other) noexcept;
  ServerAddress& operator=(ServerAddress&& other) noexcept;

  bool operator==(const ServerAddress& other) const {
    return Compare(other) == 0;
  }

  int Compare(const ServerAddress& other) const;

  const grpc_resolved_address& address() const { return address_; }
  const grpc_channel_args* args() const { return args_; }
  const ResolverAttributeMap& subchannel_attributes() const {
    return subchannel_attributes_;
  }
  const ResolverAttributeMap& lb_policy_attributes() const {
    return lb_policy_attributes_;
  }

  // TODO(ctiller): Prior to making this a public API we should ensure that the
  // channel args are not part of the generated string, lest we make that debug
  // format load-bearing via Hyrum's law.
  std::string ToString() const;

 private:
  grpc_resolved_address address_;
  grpc_channel_args* args_;
  ResolverAttributeMap subchannel_attributes_;
  ResolverAttributeMap lb_policy_attributes_;
};

//
// ServerAddressList
//

typedef absl::InlinedVector<ServerAddress, 1> ServerAddressList;

//
// ServerAddressWeightAttribute
//

class ServerAddressWeightAttribute
    : public ResolverAttributeMap::AttributeInterface {
 public:
  explicit ServerAddressWeightAttribute(uint32_t weight) : weight_(weight) {}

  uint32_t weight() const { return weight_; }

  static UniqueTypeName Type();

  UniqueTypeName type() const override { return Type(); }

  std::unique_ptr<AttributeInterface> Copy() const override {
    return absl::make_unique<ServerAddressWeightAttribute>(weight_);
  }

  int Compare(const AttributeInterface* other) const override {
    const auto* other_locality_attr =
        static_cast<const ServerAddressWeightAttribute*>(other);
    return QsortCompare(weight_, other_locality_attr->weight_);
  }

  std::string ToString() const override;

 private:
  uint32_t weight_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H
