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

#ifndef GRPC_SRC_CORE_HANDSHAKER_HTTP_CONNECT_HTTPS_PROXY_TLS_HANDSHAKER_H
#define GRPC_SRC_CORE_HANDSHAKER_HTTP_CONNECT_HTTPS_PROXY_TLS_HANDSHAKER_H

#include <grpc/support/port_platform.h>

#include "src/core/config/core_configuration.h"

namespace grpc_core {

/// Registers the HTTPS proxy TLS handshaker factory.
/// This handshaker performs TLS negotiation with an HTTPS proxy before
/// the HTTP CONNECT request is sent.
void RegisterHttpsProxyTlsHandshaker(CoreConfiguration::Builder* builder);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_HANDSHAKER_HTTP_CONNECT_HTTPS_PROXY_TLS_HANDSHAKER_H
