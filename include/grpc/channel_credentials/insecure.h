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

#ifndef GRPC_CHANNEL_CREDENTIALS_INSECURE_H
#define GRPC_CHANNEL_CREDENTIALS_INSECURE_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This method creates an insecure channel credentials object.
 */
GRPCAPI grpc_channel_credentials* grpc_insecure_credentials_create();

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This method creates an insecure server credentials object.
 */
GRPCAPI grpc_server_credentials* grpc_insecure_server_credentials_create();

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CHANNEL_CREDENTIALS_INSECURE_H */
