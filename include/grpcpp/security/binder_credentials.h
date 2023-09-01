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

#ifndef GRPCPP_SECURITY_BINDER_CREDENTIALS_H
#define GRPCPP_SECURITY_BINDER_CREDENTIALS_H

#include <memory>

#include <grpcpp/security/binder_security_policy.h>
#include <grpcpp/security/server_credentials.h>

namespace grpc {

class ChannelCredentials;

namespace experimental {

/// EXPERIMENTAL Builds Binder ServerCredentials.
///
/// This should be used along with `binder:` URI scheme. The path in the URI can
/// later be used to access the server's endpoint binder.
/// Note that calling \a ServerBuilder::AddListeningPort() with Binder
/// ServerCredentials in a non-supported environment will make the subsequent
/// call to \a ServerBuilder::BuildAndStart() return a null pointer.
std::shared_ptr<grpc::ServerCredentials> BinderServerCredentials(
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy);

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_BINDER_CREDENTIALS_H
