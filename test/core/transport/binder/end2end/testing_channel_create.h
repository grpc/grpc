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

#ifndef GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_TESTING_CHANNEL_CREATE_H
#define GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_TESTING_CHANNEL_CREATE_H

#include <utility>

#include <grpcpp/grpcpp.h>

#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/lib/surface/server.h"
#include "test/core/transport/binder/end2end/fake_binder.h"

namespace grpc_binder {
namespace end2end_testing {

std::pair<grpc_core::Transport*, grpc_core::Transport*>
CreateClientServerBindersPairForTesting();

std::shared_ptr<grpc::Channel> BinderChannelForTesting(
    grpc::Server* server, const grpc::ChannelArguments& args);

}  // namespace end2end_testing
}  // namespace grpc_binder

grpc_channel* grpc_binder_channel_create_for_testing(
    grpc_server* server, const grpc_channel_args* args, void* /*reserved*/);

#endif  // GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_TESTING_CHANNEL_CREATE_H
