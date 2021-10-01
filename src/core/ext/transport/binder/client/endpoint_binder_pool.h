// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_ENDPOINT_BINDER_POOL_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_ENDPOINT_BINDER_POOL_H

#include <functional>
#include <map>
#include <string>

#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

class EndpointBinderPool {
 public:
  // Invokes the callback when the binder corresponding to the conn_id become
  // available. If the binder is already available, invokes the callback
  // immediately.
  void GetEndpointBinder(
      std::string conn_id,
      std::function<void(std::unique_ptr<grpc_binder::Binder>)> cb);
  void AddEndpointBinder(std::string conn_id,
                         std::unique_ptr<grpc_binder::Binder> b);

 private:
  grpc_core::Mutex m_;
  std::map<std::string, std::unique_ptr<grpc_binder::Binder>> binder_map_
      ABSL_GUARDED_BY(m_);
  std::map<std::string,
           std::function<void(std::unique_ptr<grpc_binder::Binder>)>>
      pending_requests_ ABSL_GUARDED_BY(m_);
};

EndpointBinderPool* GetEndpointBinderPool();

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_ENDPOINT_BINDER_POOL_H
