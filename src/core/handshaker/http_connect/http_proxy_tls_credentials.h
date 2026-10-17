//
//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_HANDSHAKER_HTTP_CONNECT_HTTP_PROXY_TLS_CREDENTIALS_H
#define GRPC_SRC_CORE_HANDSHAKER_HTTP_CONNECT_HTTP_PROXY_TLS_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/ref_counted_ptr.h"

struct grpc_channel_credentials;

namespace grpc_core {

/// Creates TLS credentials for connecting to an HTTPS proxy.
/// This function reads the proxy TLS configuration from channel args:
/// - GRPC_ARG_HTTP_PROXY_TLS_ROOT_CERTS: Root certificates for verification
/// - GRPC_ARG_HTTP_PROXY_TLS_VERIFY_SERVER_CERT: Whether to verify server cert
/// - GRPC_ARG_HTTP_PROXY_TLS_SERVER_NAME: Expected server name for verification
/// - GRPC_ARG_HTTP_PROXY_TLS_CERT_CHAIN: Client certificate for mTLS
/// - GRPC_ARG_HTTP_PROXY_TLS_PRIVATE_KEY: Client private key for mTLS
///
/// Returns nullptr if credentials cannot be created.
RefCountedPtr<grpc_channel_credentials> CreateHttpProxyTlsCredentials(
    const ChannelArgs& args);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_HANDSHAKER_HTTP_CONNECT_HTTP_PROXY_TLS_CREDENTIALS_H
