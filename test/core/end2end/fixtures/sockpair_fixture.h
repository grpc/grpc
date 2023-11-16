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

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
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
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {

class SockpairFixture : public CoreTestFixture {
 public:
  explicit SockpairFixture(const ChannelArgs& ep_args)
      : ep_(grpc_iomgr_create_endpoint_pair("fixture", ep_args.ToC().get())) {}

  ~SockpairFixture() override {
    ExecCtx exec_ctx;
    if (ep_.client != nullptr) {
      grpc_endpoint_shutdown(ep_.client, absl::InternalError("done"));
      grpc_endpoint_destroy(ep_.client);
    }
    if (ep_.server != nullptr) {
      grpc_endpoint_shutdown(ep_.server, absl::InternalError("done"));
      grpc_endpoint_destroy(ep_.server);
    }
  }

 private:
  virtual ChannelArgs MutateClientArgs(ChannelArgs args) { return args; }
  virtual ChannelArgs MutateServerArgs(ChannelArgs args) { return args; }
  grpc_server* MakeServer(
      const ChannelArgs& in_args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    auto args = MutateServerArgs(in_args);
    ExecCtx exec_ctx;
    Transport* transport;
    auto* server = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(server, cq, nullptr);
    pre_server_start(server);
    grpc_server_start(server);
    auto server_channel_args = CoreConfiguration::Get()
                                   .channel_args_preconditioning()
                                   .PreconditionChannelArgs(args.ToC().get());
    auto* server_endpoint = std::exchange(ep_.server, nullptr);
    EXPECT_NE(server_endpoint, nullptr);
    transport = grpc_create_chttp2_transport(server_channel_args,
                                             server_endpoint, false);
    grpc_endpoint_add_to_pollset(server_endpoint, grpc_cq_pollset(cq));
    Server* core_server = Server::FromC(server);
    grpc_error_handle error = core_server->SetupTransport(
        transport, nullptr, core_server->channel_args(), nullptr);
    if (error.ok()) {
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    } else {
      transport->Orphan();
    }
    return server;
  }
  grpc_channel* MakeClient(const ChannelArgs& in_args,
                           grpc_completion_queue*) override {
    ExecCtx exec_ctx;
    auto args = CoreConfiguration::Get()
                    .channel_args_preconditioning()
                    .PreconditionChannelArgs(
                        MutateClientArgs(in_args)
                            .Set(GRPC_ARG_DEFAULT_AUTHORITY, "test-authority")
                            .ToC()
                            .get());
    Transport* transport;
    auto* client_endpoint = std::exchange(ep_.client, nullptr);
    EXPECT_NE(client_endpoint, nullptr);
    transport = grpc_create_chttp2_transport(args, client_endpoint, true);
    auto channel = Channel::Create("socketpair-target", args,
                                   GRPC_CLIENT_DIRECT_CHANNEL, transport);
    grpc_channel* client;
    if (channel.ok()) {
      client = channel->release()->c_ptr();
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    } else {
      client = grpc_lame_client_channel_create(
          nullptr, static_cast<grpc_status_code>(channel.status().code()),
          "lame channel");
      transport->Orphan();
    }
    GPR_ASSERT(client);
    return client;
  }

  grpc_endpoint_pair ep_;
};
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_SOCKPAIR_FIXTURE_H
