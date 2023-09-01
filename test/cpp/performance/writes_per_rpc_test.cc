//
//
// Copyright 2017 gRPC authors.
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

#include <gtest/gtest.h>

#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"
#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/passthru_endpoint.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {

static void* tag(intptr_t x) { return reinterpret_cast<void*>(x); }

static void ApplyCommonServerBuilderConfig(ServerBuilder* b) {
  b->SetMaxReceiveMessageSize(INT_MAX);
  b->SetMaxSendMessageSize(INT_MAX);
}

static void ApplyCommonChannelArguments(grpc_core::ChannelArgs* c) {
  *c = c->Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, INT_MAX)
           .Set(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, INT_MAX);
}

class EndpointPairFixture {
 public:
  EndpointPairFixture(Service* service, grpc_endpoint_pair endpoints) {
    ServerBuilder b;
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();

    grpc_core::ExecCtx exec_ctx;

    // add server endpoint to server_
    {
      grpc_core::Server* core_server =
          grpc_core::Server::FromC(server_->c_server());
      grpc_transport* transport = grpc_create_chttp2_transport(
          core_server->channel_args(), endpoints.server, false /* is_client */);
      for (grpc_pollset* pollset : core_server->pollsets()) {
        grpc_endpoint_add_to_pollset(endpoints.server, pollset);
      }

      GPR_ASSERT(GRPC_LOG_IF_ERROR(
          "SetupTransport",
          core_server->SetupTransport(transport, nullptr,
                                      core_server->channel_args(), nullptr)));
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);
    }

    // create channel
    {
      grpc_core::ChannelArgs args =
          grpc_core::CoreConfiguration::Get()
              .channel_args_preconditioning()
              .PreconditionChannelArgs(nullptr)
              .Set(GRPC_ARG_DEFAULT_AUTHORITY, "test.authority");
      ApplyCommonChannelArguments(&args);

      grpc_transport* transport =
          grpc_create_chttp2_transport(args, endpoints.client, true);
      GPR_ASSERT(transport);
      grpc_channel* channel =
          grpc_core::Channel::Create("target", args, GRPC_CLIENT_DIRECT_CHANNEL,
                                     transport)
              ->release()
              ->c_ptr();
      grpc_chttp2_transport_start_reading(transport, nullptr, nullptr, nullptr);

      channel_ = grpc::CreateChannelInternal(
          "", channel,
          std::vector<std::unique_ptr<
              experimental::ClientInterceptorFactoryInterface>>());
    }
  }

  virtual ~EndpointPairFixture() {
    server_->Shutdown();
    cq_->Shutdown();
    void* tag;
    bool ok;
    while (cq_->Next(&tag, &ok)) {
    }
  }

  ServerCompletionQueue* cq() { return cq_.get(); }
  std::shared_ptr<Channel> channel() { return channel_; }

 private:
  std::unique_ptr<Server> server_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::shared_ptr<Channel> channel_;
};

class InProcessCHTTP2 : public EndpointPairFixture {
 public:
  InProcessCHTTP2(Service* service, grpc_passthru_endpoint_stats* stats)
      : EndpointPairFixture(service, MakeEndpoints(stats)), stats_(stats) {}

  ~InProcessCHTTP2() override {
    if (stats_ != nullptr) {
      grpc_passthru_endpoint_stats_destroy(stats_);
    }
  }

  int writes_performed() const { return gpr_atm_acq_load(&stats_->num_writes); }

 private:
  grpc_passthru_endpoint_stats* stats_;

  static grpc_endpoint_pair MakeEndpoints(grpc_passthru_endpoint_stats* stats) {
    grpc_endpoint_pair p;
    grpc_passthru_endpoint_create(&p.client, &p.server, stats);
    return p;
  }
};

static double UnaryPingPong(int request_size, int response_size) {
  const int kIterations = 10000;

  EchoTestService::AsyncService service;
  std::unique_ptr<InProcessCHTTP2> fixture(
      new InProcessCHTTP2(&service, grpc_passthru_endpoint_stats_create()));
  EchoRequest send_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  if (request_size > 0) {
    send_request.set_message(std::string(request_size, 'a'));
  }
  if (response_size > 0) {
    send_response.set_message(std::string(response_size, 'a'));
  }
  Status recv_status;
  struct ServerEnv {
    ServerContext ctx;
    EchoRequest recv_request;
    grpc::ServerAsyncResponseWriter<EchoResponse> response_writer;
    ServerEnv() : response_writer(&ctx) {}
  };
  uint8_t server_env_buffer[2 * sizeof(ServerEnv)];
  ServerEnv* server_env[2] = {
      reinterpret_cast<ServerEnv*>(server_env_buffer),
      reinterpret_cast<ServerEnv*>(server_env_buffer + sizeof(ServerEnv))};
  new (server_env[0]) ServerEnv;
  new (server_env[1]) ServerEnv;
  service.RequestEcho(&server_env[0]->ctx, &server_env[0]->recv_request,
                      &server_env[0]->response_writer, fixture->cq(),
                      fixture->cq(), tag(0));
  service.RequestEcho(&server_env[1]->ctx, &server_env[1]->recv_request,
                      &server_env[1]->response_writer, fixture->cq(),
                      fixture->cq(), tag(1));
  std::unique_ptr<EchoTestService::Stub> stub(
      EchoTestService::NewStub(fixture->channel()));
  for (int iteration = 0; iteration < kIterations; iteration++) {
    recv_response.Clear();
    ClientContext cli_ctx;
    std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
        stub->AsyncEcho(&cli_ctx, send_request, fixture->cq()));
    void* t;
    bool ok;
    response_reader->Finish(&recv_response, &recv_status, tag(4));
    GPR_ASSERT(fixture->cq()->Next(&t, &ok));
    GPR_ASSERT(ok);
    GPR_ASSERT(t == tag(0) || t == tag(1));
    intptr_t slot = reinterpret_cast<intptr_t>(t);
    ServerEnv* senv = server_env[slot];
    senv->response_writer.Finish(send_response, Status::OK, tag(3));
    for (int i = (1 << 3) | (1 << 4); i != 0;) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int tagnum = static_cast<int>(reinterpret_cast<intptr_t>(t));
      GPR_ASSERT(i & (1 << tagnum));
      i -= 1 << tagnum;
    }
    GPR_ASSERT(recv_status.ok());

    senv->~ServerEnv();
    senv = new (senv) ServerEnv();
    service.RequestEcho(&senv->ctx, &senv->recv_request, &senv->response_writer,
                        fixture->cq(), fixture->cq(), tag(slot));
  }

  double writes_per_iteration =
      static_cast<double>(fixture->writes_performed()) /
      static_cast<double>(kIterations);

  fixture.reset();
  server_env[0]->~ServerEnv();
  server_env[1]->~ServerEnv();

  return writes_per_iteration;
}

TEST(WritesPerRpcTest, UnaryPingPong) {
  EXPECT_LT(UnaryPingPong(0, 0), 2.05);
  EXPECT_LT(UnaryPingPong(1, 0), 2.05);
  EXPECT_LT(UnaryPingPong(0, 1), 2.05);
  EXPECT_LT(UnaryPingPong(4096, 0), 2.5);
  EXPECT_LT(UnaryPingPong(0, 4096), 2.5);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
