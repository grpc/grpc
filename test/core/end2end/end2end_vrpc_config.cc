//
//
// Copyright 2026 gRPC authors.
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

#include <grpc/compression.h>
#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/time.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "src/core/client_channel/virtual_channel.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/server/server.h"
#include "src/core/transport/session_endpoint.h"
#include "src/core/util/grpc_check.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

// IWYU pragma: no_include <unistd.h>

namespace grpc_core {

class VrpcInsecureFixture : public InsecureFixture {
 public:
  VrpcInsecureFixture() {
    local_cq_ = grpc_completion_queue_create_for_next(nullptr);
    grpc_metadata_array_init(&server_request_metadata_);
    grpc_call_details_init(&server_call_details_);
  }

  // We cannot call InitRealServer/InitRealClient in the constructor because
  // they call virtual methods (real_client_target() and real_server_port())
  // which are overridden by derived classes (like VrpcProxyFixture).
  // Virtual dispatch doesn't work from constructors in C++, so this must
  // be called after the object is fully constructed.
  void Init() {
    InitRealServer();
    InitRealClient();
    EstablishTunnel();
  }

  ~VrpcInsecureFixture() override { Teardown(); }

  grpc_server* MakeServer(
      const ChannelArgs& args, grpc_completion_queue* cq,
      absl::AnyInvocable<void(grpc_server*)>& pre_server_start) override {
    // Create the virtual server
    virtual_server_ = grpc_server_create(args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(virtual_server_, cq, nullptr);
    pre_server_start(virtual_server_);
    grpc_server_start(virtual_server_);

    // Bind session to inner server
    {
      ExecCtx exec_ctx;
      Server* core_inner_server = Server::FromC(virtual_server_);
      grpc_endpoint* endpoint =
          SessionEndpoint::Create(server_call_, /*is_client=*/false);

      ChannelArgs server_args = core_inner_server->channel_args();
      if (server_args.GetObject<ResourceQuota>() == nullptr) {
        server_args = server_args.SetObject(ResourceQuota::Default());
      }

      Transport* transport_ptr = grpc_create_chttp2_transport(
          server_args, OrphanablePtr<grpc_endpoint>(endpoint),
          /*is_client=*/false);

      auto status = core_inner_server->SetupTransport(
          transport_ptr, nullptr, server_args, GRPC_SERVER_VIRTUAL_CHANNEL);
      GRPC_CHECK(status.ok());
      grpc_chttp2_transport_start_reading(transport_ptr, nullptr, nullptr,
                                          nullptr, nullptr);
    }

    return virtual_server_;
  }

  grpc_channel* MakeClient(const ChannelArgs& args,
                           grpc_completion_queue* /*cq*/) override {
    // Create the virtual channel using VirtualChannel::Create
    auto virtual_channel = [&]() {
      ExecCtx exec_ctx;
      auto ch = VirtualChannel::Create(client_call_, args);
      GRPC_CHECK(ch.ok());
      return ch;
    }();

    return virtual_channel->release()->c_ptr();
  }

 protected:
  virtual std::string real_client_target() { return localaddr(); }
  virtual std::string real_server_port() { return localaddr(); }

 private:
  void InitRealServer() {
    auto real_server_args = MutateServerArgs(ChannelArgs());
    auto* server_creds = grpc_insecure_server_credentials_create();
    real_server_ = grpc_server_create(real_server_args.ToC().get(), nullptr);
    grpc_server_register_completion_queue(real_server_, local_cq_, nullptr);
    GRPC_CHECK(grpc_server_add_http2_port(
        real_server_, real_server_port().c_str(), server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(real_server_);

    // Request a call on the real server
    grpc_call_error err = grpc_server_request_call(
        real_server_, &server_call_, &server_call_details_,
        &server_request_metadata_, local_cq_, local_cq_,
        reinterpret_cast<void*>(1));
    GRPC_CHECK_EQ(err, GRPC_CALL_OK);
  }

  void InitRealClient() {
    auto real_client_args = MutateClientArgs(ChannelArgs());
    auto* client_creds = grpc_insecure_credentials_create();
    real_client_ =
        grpc_channel_create(real_client_target().c_str(), client_creds,
                            real_client_args.ToC().get());
    GRPC_CHECK_NE(real_client_, nullptr);
    grpc_channel_credentials_release(client_creds);
  }

  void EstablishTunnel() {
    client_call_ = grpc_channel_create_call(
        real_client_, nullptr, GRPC_PROPAGATE_DEFAULTS, local_cq_,
        grpc_slice_from_static_string("/grpc.testing.TunnelService/Connect"),
        nullptr, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

    // Send initial metadata from client
    grpc_op ops[1];
    memset(ops, 0, sizeof(ops));
    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;

    grpc_call_error err = grpc_call_start_batch(
        client_call_, ops, 1, reinterpret_cast<void*>(2), nullptr);
    GRPC_CHECK_EQ(err, GRPC_CALL_OK);

    // Wait for the server to receive the call and client to send metadata
    bool server_received = false;
    bool client_sent = false;
    while (!server_received || !client_sent) {
      grpc_event ev = grpc_completion_queue_next(
          local_cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
      if (ev.tag == reinterpret_cast<void*>(1)) {
        server_received = true;
      } else if (ev.tag == reinterpret_cast<void*>(2)) {
        client_sent = true;
      }
    }

    // Now send initial metadata from the server
    memset(ops, 0, sizeof(ops));
    ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
    ops[0].data.send_initial_metadata.count = 0;
    err = grpc_call_start_batch(server_call_, ops, 1,
                                reinterpret_cast<void*>(3), nullptr);
    GRPC_CHECK_EQ(err, GRPC_CALL_OK);

    // Receive initial metadata on the client
    grpc_metadata_array client_recv_metadata;
    grpc_metadata_array_init(&client_recv_metadata);
    memset(ops, 0, sizeof(ops));
    ops[0].op = GRPC_OP_RECV_INITIAL_METADATA;
    ops[0].data.recv_initial_metadata.recv_initial_metadata =
        &client_recv_metadata;
    err = grpc_call_start_batch(client_call_, ops, 1,
                                reinterpret_cast<void*>(4), nullptr);
    GRPC_CHECK_EQ(err, GRPC_CALL_OK);

    // Wait for the server to send and client to receive
    bool server_sent = false;
    bool client_received = false;
    while (!server_sent || !client_received) {
      grpc_event ev = grpc_completion_queue_next(
          local_cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
      if (ev.tag == reinterpret_cast<void*>(3)) {
        server_sent = true;
      } else if (ev.tag == reinterpret_cast<void*>(4)) {
        client_received = true;
      }
    }

    grpc_metadata_array_destroy(&client_recv_metadata);
  }

  void Teardown() {
    if (client_call_ != nullptr) grpc_call_unref(client_call_);
    if (server_call_ != nullptr) grpc_call_unref(server_call_);

    if (real_client_ != nullptr) grpc_channel_destroy(real_client_);
    if (real_server_ != nullptr) {
      grpc_server_shutdown_and_notify(real_server_, local_cq_,
                                      reinterpret_cast<void*>(5));
      while (true) {
        grpc_event ev = grpc_completion_queue_next(
            local_cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
        if (ev.tag == reinterpret_cast<void*>(5)) break;
      }
      grpc_server_destroy(real_server_);
    }

    grpc_metadata_array_destroy(&server_request_metadata_);
    grpc_call_details_destroy(&server_call_details_);

    grpc_completion_queue_shutdown(local_cq_);
    while (grpc_completion_queue_next(
               local_cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr)
               .type != GRPC_QUEUE_SHUTDOWN) {
    }
    grpc_completion_queue_destroy(local_cq_);
  }

  grpc_completion_queue* local_cq_;
  grpc_server* real_server_ = nullptr;
  grpc_channel* real_client_ = nullptr;
  grpc_server* virtual_server_ = nullptr;
  grpc_call* server_call_ = nullptr;
  grpc_call_details server_call_details_;
  grpc_metadata_array server_request_metadata_;
  grpc_call* client_call_ = nullptr;
};

class VrpcNoRetryFixture : public VrpcInsecureFixture {
 private:
  ChannelArgs MutateClientArgs(ChannelArgs args) override {
    return args.Set(GRPC_ARG_ENABLE_RETRIES, false);
  }
};

class VrpcProxyFixture : public VrpcInsecureFixture {
 public:
  VrpcProxyFixture(const ChannelArgs& client_args,
                   const ChannelArgs& server_args)
      : proxy_(grpc_end2end_proxy_create(&proxy_def_, client_args.ToC().get(),
                                         server_args.ToC().get())) {}
  ~VrpcProxyFixture() override { grpc_end2end_proxy_destroy(proxy_); }

 protected:
  std::string real_client_target() override {
    return grpc_end2end_proxy_get_client_target(proxy_);
  }
  std::string real_server_port() override {
    return grpc_end2end_proxy_get_server_port(proxy_);
  }

 private:
  static grpc_server* CreateProxyServer(const char* port,
                                        const grpc_channel_args* server_args) {
    grpc_server* s = grpc_server_create(server_args, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    GRPC_CHECK(grpc_server_add_http2_port(s, port, server_creds));
    grpc_server_credentials_release(server_creds);
    return s;
  }

  static grpc_channel* CreateProxyClient(const char* target,
                                         const grpc_channel_args* client_args) {
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    grpc_channel* channel = grpc_channel_create(target, creds, client_args);
    grpc_channel_credentials_release(creds);
    return channel;
  }

  ChannelArgs MutateServerArgs(ChannelArgs args) override { return args; }

  ChannelArgs MutateClientArgs(ChannelArgs args) override { return args; }

  const grpc_end2end_proxy_def proxy_def_ = {CreateProxyServer,
                                             CreateProxyClient};
  grpc_end2end_proxy* proxy_;
};

std::vector<CoreTestConfiguration> End2endTestConfigs() {
  if (!IsEventEngineClientEnabled() || !IsEventEngineListenerEnabled()) {
    return {};
  }

  return std::vector<CoreTestConfiguration>{
      CoreTestConfiguration{
          /*name=*/"VrpcOverChttp2Fullstack",
          /*feature_mask=*/FEATURE_MASK_IS_VIRTUAL_RPC | FEATURE_MASK_IS_HTTP2,
          /*overridden_call_host=*/nullptr,
          /*create_fixture=*/
          [](const ChannelArgs& /*client_args*/,
             const ChannelArgs& /*server_args*/) {
            auto f = std::make_unique<VrpcInsecureFixture>();
            f->Init();
            return f;
          }},
      CoreTestConfiguration{/*name=*/"VrpcOverChttp2FullstackNoRetry",
                            /*feature_mask=*/FEATURE_MASK_IS_VIRTUAL_RPC |
                                FEATURE_MASK_IS_HTTP2 |
                                FEATURE_MASK_DOES_NOT_SUPPORT_RETRY,
                            /*overridden_call_host=*/nullptr,
                            /*create_fixture=*/
                            [](const ChannelArgs& /*client_args*/,
                               const ChannelArgs& /*server_args*/) {
                              auto f = std::make_unique<VrpcNoRetryFixture>();
                              f->Init();
                              return f;
                            }},
      CoreTestConfiguration{
          /*name=*/"VrpcOverChttp2FullstackWithProxy",
          /*feature_mask=*/FEATURE_MASK_IS_VIRTUAL_RPC | FEATURE_MASK_IS_HTTP2 |
              FEATURE_MASK_DO_NOT_FUZZ,
          /*overridden_call_host=*/nullptr,
          /*create_fixture=*/
          [](const ChannelArgs& client_args, const ChannelArgs& server_args) {
            auto f =
                std::make_unique<VrpcProxyFixture>(client_args, server_args);
            f->Init();
            return f;
          },
      },
  };
}

}  // namespace grpc_core
