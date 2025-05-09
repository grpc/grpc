// Copyright 2025 The gRPC Authors
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
#ifndef GRPC_SECURE_POSIX_H
#define GRPC_SECURE_POSIX_H

#include <grpc/grpc.h>

#include <memory>

#include "event_engine/event_engine.h"
namespace grpc_core::experimental {

grpc_channel* CreateChannelFromEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    grpc_channel_credentials* creds, const grpc_channel_args* args);

grpc_channel* CreateChannelFromFd(int fd, grpc_channel_credentials* creds,
                                  const grpc_channel_args* args);

}  // namespace grpc_core::experimental

#endif /* GRPC_SECURE_POSIX_H */