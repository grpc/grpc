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

#ifndef GRPC_CHANNEL_CREDENTIALS_LOCAL_H
#define GRPC_CHANNEL_CREDENTIALS_LOCAL_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>

#ifdef __cplusplus
extern "C" {
#endif

/** --- Local channel/server credentials --- **/

/**
 * This method creates a local channel credential object. The security level
 * of the resulting connection is GRPC_PRIVACY_AND_INTEGRITY for UDS and
 * GRPC_SECURITY_NONE for LOCAL_TCP. It is used for experimental purpose
 * for now and subject to change.
 *
 * - type: local connection type
 *
 * It returns the created local channel credential object.
 */
GRPCAPI grpc_channel_credentials* grpc_local_credentials_create(
    grpc_local_connect_type type);

/**
 * This method creates a local server credential object. It is used for
 * experimental purpose for now and subject to change.
 *
 * - type: local connection type
 *
 * It returns the created local server credential object.
 */
GRPCAPI grpc_server_credentials* grpc_local_server_credentials_create(
    grpc_local_connect_type type);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CHANNEL_CREDENTIALS_LOCAL_H */
