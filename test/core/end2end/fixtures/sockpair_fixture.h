// Copyright 2023 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_SOCKPAIR_FIXTURE_H
#define GRPC_TEST_CORE_END2END_FIXTURES_SOCKPAIR_FIXTURE_H

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "test/core/end2end/end2end_tests.h"

class SockpairFixture : public CoreTestFixture {
 public:
  explicit SockpairFixture(const grpc_core::ChannelArgs& ep_args)
      : ep_(grpc_iomgr_create_endpoint_pair("fixture", ep_args.ToC().get())) {}

 private:
  virtual grpc_core::ChannelArgs MutateClientArgs(grpc_core::ChannelArgs args) {
    return args;
  }
  virtual grpc_core::ChannelArgs MutateServerArgs(grpc_core::ChannelArgs args) {
    return args;
  }
  grpc_server* MakeServer(const grpc_core::ChannelArgs& in_args) override {
    auto args = MutateServerArgs(in_args);
    grpc_core::ExecCtx exec_ctx;
    grpc_transport* transport;
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq(), nullptr);
    grpc_server_start(server);
    auto server_channel_args = grpc_core::CoreConfiguration::Get()
                                   .channel_args_preconditioning()
                                   .PreconditionChannelArgs(args.ToC().get());
    transport =
        grpc_create_chttp2_transport(server_channel_args, ep_.server, false);
    grpc_endpoint_add_to_pollset(ep_.server, grpc_cq_pollset(cq()));
    grpc_core::Server* core_server = grpc_core::Server::FromC(server);
    grpc_error_handle error = core_server->SetupTransport(
        transport, nullptr, core_server->channel_args(), nullptr);
    if (error.ok()) {
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    } else {
      grpc_transport_destroy(transport);
    }
    return server;
  }
  grpc_channel* MakeClient(const grpc_core::ChannelArgs& in_args) override {
    grpc_core::ExecCtx exec_ctx;
    auto args = grpc_core::CoreConfiguration::Get()
                    .channel_args_preconditioning()
                    .PreconditionChannelArgs(
                        MutateClientArgs(in_args)
                            .Set(GRPC_ARG_DEFAULT_AUTHORITY, "test-authority")
                            .ToC()
                            .get());
    grpc_transport* transport;
    transport = grpc_create_chttp2_transport(args, ep_.client, true);
    auto channel = grpc_core::Channel::Create(
        "socketpair-target", args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
    grpc_channel* client;
    if (channel.ok()) {
      client = channel->release()->c_ptr();
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    } else {
      client = grpc_lame_client_channel_create(
          nullptr, static_cast<grpc_status_code>(channel.status().code()),
          "lame channel");
      grpc_transport_destroy(transport);
    }
    GPR_ASSERT(client);
    return client;
  }

  grpc_endpoint_pair ep_;
};

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_SOCKPAIR_FIXTURE_H
