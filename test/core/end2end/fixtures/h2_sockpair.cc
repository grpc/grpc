//
//
// Copyright 2015 gRPC authors.
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

#include <string.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
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
#include "test/core/util/test_config.h"

// chttp2 transport that is immediately available (used for testing
// connected_channel without a client_channel

struct custom_fixture_data {
  grpc_endpoint_pair ep;
};

static void server_setup_transport(void* ts, grpc_transport* transport) {
  grpc_end2end_test_fixture* f = static_cast<grpc_end2end_test_fixture*>(ts);
  grpc_core::ExecCtx exec_ctx;
  custom_fixture_data* fixture_data =
      static_cast<custom_fixture_data*>(f->fixture_data);
  grpc_endpoint_add_to_pollset(fixture_data->ep.server, grpc_cq_pollset(f->cq));
  grpc_core::Server* core_server = grpc_core::Server::FromC(f->server);
  grpc_error_handle error = core_server->SetupTransport(
      transport, nullptr, core_server->channel_args(), nullptr);
  if (error.ok()) {
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
  } else {
    grpc_transport_destroy(transport);
  }
}

typedef struct {
  grpc_end2end_test_fixture* f;
  const grpc_channel_args* client_args;
} sp_client_setup;

static void client_setup_transport(void* ts, grpc_transport* transport) {
  sp_client_setup* cs = static_cast<sp_client_setup*>(ts);

  auto args = grpc_core::ChannelArgs::FromC(cs->client_args)
                  .Set(GRPC_ARG_DEFAULT_AUTHORITY, "test-authority");
  auto channel = grpc_core::Channel::Create(
      "socketpair-target", args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
  if (channel.ok()) {
    cs->f->client = channel->release()->c_ptr();
    grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
  } else {
    cs->f->client = grpc_lame_client_channel_create(
        nullptr, static_cast<grpc_status_code>(channel.status().code()),
        "lame channel");
    grpc_transport_destroy(transport);
  }
}

static grpc_end2end_test_fixture chttp2_create_fixture_socketpair(
    const grpc_channel_args* /*client_args*/,
    const grpc_channel_args* /*server_args*/) {
  custom_fixture_data* fixture_data = static_cast<custom_fixture_data*>(
      gpr_malloc(sizeof(custom_fixture_data)));
  grpc_end2end_test_fixture f;
  memset(&f, 0, sizeof(f));
  f.fixture_data = fixture_data;
  f.cq = grpc_completion_queue_create_for_next(nullptr);
  fixture_data->ep = grpc_iomgr_create_endpoint_pair("fixture", nullptr);
  return f;
}

static void chttp2_init_client_socketpair(
    grpc_end2end_test_fixture* f, const grpc_channel_args* client_args) {
  grpc_core::ExecCtx exec_ctx;
  auto* fixture_data = static_cast<custom_fixture_data*>(f->fixture_data);
  grpc_transport* transport;
  sp_client_setup cs;
  auto client_channel_args = grpc_core::CoreConfiguration::Get()
                                 .channel_args_preconditioning()
                                 .PreconditionChannelArgs(client_args);
  cs.client_args = client_channel_args.ToC().release();
  cs.f = f;
  transport = grpc_create_chttp2_transport(client_channel_args,
                                           fixture_data->ep.client, true);
  client_setup_transport(&cs, transport);
  grpc_channel_args_destroy(cs.client_args);
  GPR_ASSERT(f->client);
}

static void chttp2_init_server_socketpair(
    grpc_end2end_test_fixture* f, const grpc_channel_args* server_args) {
  grpc_core::ExecCtx exec_ctx;
  auto* fixture_data = static_cast<custom_fixture_data*>(f->fixture_data);
  grpc_transport* transport;
  GPR_ASSERT(!f->server);
  f->server = grpc_server_create(server_args, nullptr);
  grpc_server_register_completion_queue(f->server, f->cq, nullptr);
  grpc_server_start(f->server);
  auto server_channel_args = grpc_core::CoreConfiguration::Get()
                                 .channel_args_preconditioning()
                                 .PreconditionChannelArgs(server_args);
  transport = grpc_create_chttp2_transport(server_channel_args,
                                           fixture_data->ep.server, false);
  server_setup_transport(f, transport);
}

static void chttp2_tear_down_socketpair(grpc_end2end_test_fixture* f) {
  grpc_core::ExecCtx exec_ctx;
  gpr_free(f->fixture_data);
}

// All test configurations
static grpc_end2end_test_config configs[] = {
    {"chttp2/socketpair", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
     chttp2_create_fixture_socketpair, chttp2_init_client_socketpair,
     chttp2_init_server_socketpair, chttp2_tear_down_socketpair},
};

int main(int argc, char** argv) {
  size_t i;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
