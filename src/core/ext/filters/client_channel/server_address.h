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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVER_ADDRESS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVER_ADDRESS_H

#include <grpc/support/port_platform.h>

#include <map>

#include "absl/container/inlined_vector.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolve_address.h"

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
  class AttributeInterface {
   public:
    virtual ~AttributeInterface();

    // Creates a copy of the attribute.
    virtual std::unique_ptr<AttributeInterface> Copy() const = 0;

    // Compares this attribute with another.
    virtual int Cmp(const AttributeInterface* other) const = 0;
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
  ServerAddress(const ServerAddress& other)
      : address_(other.address_), args_(grpc_channel_args_copy(other.args_)) {
    for (const auto& p : other.attributes_) {
      attributes_[p.first] = p.second->Copy();
    }
  }
  ServerAddress& operator=(const ServerAddress& other) {
    address_ = other.address_;
    grpc_channel_args_destroy(args_);
    args_ = grpc_channel_args_copy(other.args_);
    attributes_.clear();
    for (const auto& p : other.attributes_) {
      attributes_[p.first] = p.second->Copy();
    }
    return *this;
  }

  // Movable.
  ServerAddress(ServerAddress&& other)
      : address_(other.address_),
        args_(other.args_),
        attributes_(std::move(other.attributes_)) {
    other.args_ = nullptr;
  }
  ServerAddress& operator=(ServerAddress&& other) {
    address_ = other.address_;
    grpc_channel_args_destroy(args_);
    args_ = other.args_;
    other.args_ = nullptr;
    attributes_ = std::move(other.attributes_);
    return *this;
  }

  bool operator==(const ServerAddress& other) const { return Cmp(other) == 0; }

  int Cmp(const ServerAddress& other) const;

  const grpc_resolved_address& address() const { return address_; }
  const grpc_channel_args* args() const { return args_; }

  const AttributeInterface* GetAttribute(const char* key) const {
    auto it = attributes_.find(key);
    if (it == attributes_.end()) return nullptr;
    return it->second.get();
  }

 private:
  grpc_resolved_address address_;
  grpc_channel_args* args_;
  std::map<const char*, std::unique_ptr<AttributeInterface>> attributes_;
};

//
// ServerAddressList
//

typedef absl::InlinedVector<ServerAddress, 1> ServerAddressList;

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SERVER_ADDRESS_H */
