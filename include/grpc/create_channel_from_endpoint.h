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

#ifndef GRPC_CREATE_CHANNEL_FROM_ENDPOINT_H
#define GRPC_CREATE_CHANNEL_FROM_ENDPOINT_H

#include <grpc/grpc.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/support/port_platform.h>
#include <stddef.h>

#ifdef __cplusplus
#include <grpc/event_engine/event_engine.h>

#include <memory>
namespace grpc_core::experimental {

/**
 * EXPERIMENTAL API - Subject to change
 *
 * This function creates a gRPC channel using a pre-established
 * endpoint from the EventEngine. This API supports both secure and insecure
 * channel credentials.
 *
 * \param endpoint A unique pointer to an EventEngine endpoint representing
 *        an established connection.
 * \param creds The channel credentials used to secure the connection.
 * \param args Optional channel arguments to configure the channel behavior.
 */
grpc_channel* CreateChannelFromEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    grpc_channel_credentials* creds, const grpc_channel_args* args);

}  // namespace grpc_core::experimental

#endif  // __cplusplus

#endif /* GRPC_CREATE_CHANNEL_FROM_ENDPOINT_H */
