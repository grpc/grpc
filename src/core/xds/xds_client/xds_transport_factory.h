//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_TRANSPORT_FACTORY_H
#define GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_TRANSPORT_FACTORY_H

#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/status/status.h"

namespace grpc_core {

// A factory for creating new XdsTransport instances.
class XdsTransportFactory : public DualRefCounted<XdsTransportFactory> {
 public:
  // Returns a transport for the specified server.  If there is already
  // a transport for the server, returns a new ref to that transport;
  // otherwise, creates a new transport.
  //
  // *status will be set if there is an error creating the channel,
  // although the returned channel must still accept calls (which may fail).
  virtual RefCountedPtr<XdsTransport> GetTransport(
      const XdsBootstrap::XdsServerTarget& server, absl::Status* status) = 0;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_TRANSPORT_FACTORY_H
