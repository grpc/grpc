//
// Copyright 2019 gRPC authors.
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

#ifndef GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H
#define GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/lib/json/json.h"

namespace grpc_core {

bool XdsFederationEnabled();

class XdsBootstrap {
 public:
  class Node {
   public:
    virtual ~Node() = default;

    virtual const std::string& id() const = 0;
    virtual const std::string& cluster() const = 0;
    virtual const std::string& locality_region() const = 0;
    virtual const std::string& locality_zone() const = 0;
    virtual const std::string& locality_sub_zone() const = 0;
    virtual const Json::Object& metadata() const = 0;
  };

  class XdsServer {
   public:
    virtual ~XdsServer() = default;

    virtual const std::string& server_uri() const = 0;
    virtual bool IgnoreResourceDeletion() const = 0;

    virtual bool Equals(const XdsServer& other) const = 0;

    friend bool operator==(const XdsServer& a, const XdsServer& b) {
      return a.Equals(b);
    }
  };

  class Authority {
   public:
    virtual ~Authority() = default;

    virtual const XdsServer* server() const = 0;
  };

  virtual ~XdsBootstrap() = default;

  virtual std::string ToString() const = 0;

  // TODO(roth): We currently support only one server. Fix this when we
  // add support for fallback for the xds channel.
  virtual const XdsServer& server() const = 0;

  // Returns the node information, or null if not present in the bootstrap
  // config.
  virtual const Node* node() const = 0;

  // Returns a pointer to the specified authority, or null if it does
  // not exist in this bootstrap config.
  virtual const Authority* LookupAuthority(const std::string& name) const = 0;

  // If the server exists in the bootstrap config, returns a pointer to
  // the XdsServer instance in the config.  Otherwise, returns null.
  virtual const XdsServer* FindXdsServer(const XdsServer& server) const = 0;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_XDS_BOOTSTRAP_H
