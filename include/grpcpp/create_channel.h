//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPCPP_CREATE_CHANNEL_H
#define GRPCPP_CREATE_CHANNEL_H

#include <grpcpp/channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/client_interceptor.h>
#include <grpcpp/support/config.h>

#include <memory>
#include <string>

#include "absl/status/status.h"

namespace grpc {
/// Inform the gRPC library of the universe domain in which the process is
/// running (it is the caller's responsibility to ensure that this is correct).
/// This setting is used by the "google-c2p" resolver (the resolver used for
/// URIs with "google-c2p" schemes) to configure its dependencies. If a gRPC
/// channel is created with the "google-c2p" URI scheme and this API has not
/// yet been invoked, then gRPC configures the universe domain as
/// "googleapis.com".
///
/// \param universe_domain DNS domain for Google APIs in the current universe
/// (for example, "googleapis.com").
/// \return absl::OkStatus() if the universe domain has not yet been configured
/// or was already configured to the same value; otherwise returns a non-OK
/// status.
absl::Status GoogleC2PSetUniverseDomain(std::string universe_domain);
/// Create a new \a Channel pointing to \a target.
///
/// \param target The URI of the endpoint to connect to.
/// \param creds Credentials to use for the created channel. If it does not
/// hold an object or is invalid, a lame channel (one on which all operations
/// fail) is returned.
std::shared_ptr<Channel> CreateChannel(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds);

/// Create a new \em custom \a Channel pointing to \a target.
///
/// \warning For advanced use and testing ONLY. Override default channel
/// arguments only if necessary.
///
/// \param target The URI of the endpoint to connect to.
/// \param creds Credentials to use for the created channel. If it does not
/// hold an object or is invalid, a lame channel (one on which all operations
/// fail) is returned.
/// \param args Options for channel creation.
std::shared_ptr<Channel> CreateCustomChannel(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args);

namespace experimental {
/// Create a new \em custom \a Channel pointing to \a target with \a
/// interceptors being invoked per call.
///
/// \warning For advanced use and testing ONLY. Override default channel
/// arguments only if necessary.
///
/// \param target The URI of the endpoint to connect to.
/// \param creds Credentials to use for the created channel. If it does not
/// hold an object or is invalid, a lame channel (one on which all operations
/// fail) is returned.
/// \param args Options for channel creation.
std::shared_ptr<Channel> CreateCustomChannelWithInterceptors(
    const grpc::string& target,
    const std::shared_ptr<ChannelCredentials>& creds,
    const ChannelArguments& args,
    std::vector<
        std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators);
}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_CREATE_CHANNEL_H
