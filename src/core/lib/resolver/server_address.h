/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H
#define GRPC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/resolved_address.h"

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

  // Takes ownership of args.
  ServerAddress(const grpc_resolved_address& address, grpc_channel_args* args,
                std::map<const char*, std::unique_ptr<AttributeInterface>>
                    attributes = {});
  ServerAddress(const void* address, size_t address_len,
                grpc_channel_args* args,
                std::map<const char*, std::unique_ptr<AttributeInterface>>
                    attributes = {});

  ~ServerAddress() { grpc_channel_args_destroy(args_); }

  // Copyable.
  ServerAddress(const ServerAddress& other);
  ServerAddress& operator=(const ServerAddress& other);

  // Movable.
  ServerAddress(ServerAddress&& other) noexcept;
  ServerAddress& operator=(ServerAddress&& other) noexcept;

  bool operator==(const ServerAddress& other) const { return Cmp(other) == 0; }

  int Cmp(const ServerAddress& other) const;

  const grpc_resolved_address& address() const { return address_; }
  const grpc_channel_args* args() const { return args_; }

  const AttributeInterface* GetAttribute(const char* key) const;

  // Returns a copy of the address with a modified attribute.
  // If the new value is null, the attribute is removed.
  ServerAddress WithAttribute(const char* key,
                              std::unique_ptr<AttributeInterface> value) const;

  std::string ToString() const;

 private:
  grpc_resolved_address address_;
  grpc_channel_args* args_;
  std::map<const char*, std::unique_ptr<AttributeInterface>> attributes_;
};

//
// ServerAddressList
//

typedef absl::InlinedVector<ServerAddress, 1> ServerAddressList;

//
// ServerAddressWeightAttribute
//
class ServerAddressWeightAttribute : public ServerAddress::AttributeInterface {
 public:
  static const char* kServerAddressWeightAttributeKey;

  explicit ServerAddressWeightAttribute(uint32_t weight) : weight_(weight) {}

  uint32_t weight() const { return weight_; }

  std::unique_ptr<AttributeInterface> Copy() const override {
    return absl::make_unique<ServerAddressWeightAttribute>(weight_);
  }

  int Cmp(const AttributeInterface* other) const override {
    const auto* other_locality_attr =
        static_cast<const ServerAddressWeightAttribute*>(other);
    return QsortCompare(weight_, other_locality_attr->weight_);
  }

  std::string ToString() const override {
    return absl::StrFormat("%d", weight_);
  }

 private:
  uint32_t weight_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_RESOLVER_SERVER_ADDRESS_H */
