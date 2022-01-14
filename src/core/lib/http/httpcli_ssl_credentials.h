/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_HTTP_HTTPCLI_SSL_CREDENTIALS_H
#define GRPC_CORE_LIB_HTTP_HTTPCLI_SSL_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

namespace grpc_core {

// Creates a channel credentials suitable for use with the
// HttpCli::Get and HttpCli::Post APIs. Notably, this allows
// HTTP1 requests to use secure connections without ALPN (as the
// typicaly gRPC SSL credentials do).
//
// The result NOT INTENDED FOR USE with gRPC channels.
RefCountedPtr<grpc_channel_credentials> CreateHttpCliSSLCredentials();

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_HTTP_HTTPCLI_SSL_CREDENTIALS_H */
