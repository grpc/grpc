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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_H

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"

#include <grpc/support/port_platform.h>
#include <grpcpp/security/binder_security_policy.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/server/server.h"

// TODO(waynetu): This is part of the public API and should be moved to the
// include/ folder.
namespace grpc {
namespace experimental {
namespace binder {

void* GetEndpointBinder(const std::string& service);
void AddEndpointBinder(const std::string& service, void* endpoint_binder);
void RemoveEndpointBinder(const std::string& service);

}  // namespace binder
}  // namespace experimental
}  // namespace grpc

void grpc_add_endpoint_binder(const std::string& service,
                              void* endpoint_binder);
void grpc_remove_endpoint_binder(const std::string& service);
void* grpc_get_endpoint_binder(const std::string& service);

namespace grpc_core {

// Consume a callback, produce a transaction listener. This is used to perform
// testing in non-Android environments where the actual binder is not available.
using BinderTxReceiverFactory =
    std::function<std::unique_ptr<grpc_binder::TransactionReceiver>(
        grpc_binder::TransactionReceiver::OnTransactCb)>;

bool AddBinderPort(const std::string& addr, grpc_server* server,
                   BinderTxReceiverFactory factory,
                   std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
                       security_policy);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_SERVER_BINDER_SERVER_H
