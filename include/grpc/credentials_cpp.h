/*
 *
 * Copyright 2025 gRPC authors.
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

#ifndef GRPC_CREDENTIALS_CPP_H
#define GRPC_CREDENTIALS_CPP_H

#include <grpc/credentials.h>
#include <grpc/support/port_platform.h>

#include <optional>
#include <string>

/**
 * EXPERIMENTAL API - Subject to change
 *
 * Overrides the SNI that the client sends in the TLS handshake. nullopt
 * indicates that SNI should not be overridden. An empty string value indicates
 * that SNI should not be sent at all.
 */
void grpc_tls_credentials_options_set_sni_override(
    grpc_tls_credentials_options* options,
    std::optional<std::string> sni_override);

#endif /* GRPC_CREDENTIALS_CPP_H */
