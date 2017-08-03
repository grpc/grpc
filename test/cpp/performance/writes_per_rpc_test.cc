/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/support/log.h>
#include <gtest/gtest.h>

extern "C" {
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/tcp_posix.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/passthru_endpoint.h"
#include "test/core/util/port.h"
}
#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {

static void* tag(intptr_t x) { return reinterpret_cast<void*>(x); }

static void ApplyCommonServerBuilderConfig(ServerBuilder* b) {
  b->SetMaxReceiveMessageSize(INT_MAX);
  b->SetMaxSendMessageSize(INT_MAX);
}

static void ApplyCommonChannelArguments(ChannelArguments* c) {
  c->SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, INT_MAX);
  c->SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, INT_MAX);
}

static class InitializeStuff {
 public:
  InitializeStuff() {
    init_lib_.init();
    rq_ = grpc_resource_quota_create("bm");
  }

  ~InitializeStuff() { init_lib_.shutdown(); }

  grpc_resource_quota* rq() { return rq_; }

 private:
  internal::GrpcLibrary init_lib_;
  grpc_resource_quota* rq_;
} initialize_stuff;

class EndpointPairFixture {
 public:
  EndpointPairFixture(Service* service, grpc_endpoint_pair endpoints) {
    ServerBuilder b;
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();

    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

    /* add server endpoint to server_ */
    {
      const grpc_channel_args* server_args =
          grpc_server_get_channel_args(server_->c_server());
      grpc_transport* transport = grpc_create_chttp2_transport(
          &exec_ctx, server_args, endpoints.server, 0 /* is_client */);

      grpc_pollset** pollsets;
      size_t num_pollsets = 0;
      grpc_server_get_pollsets(server_->c_server(), &pollsets, &num_pollsets);

      for (size_t i = 0; i < num_pollsets; i++) {
        grpc_endpoint_add_to_pollset(&exec_ctx, endpoints.server, pollsets[i]);
      }

      grpc_server_setup_transport(&exec_ctx, server_->c_server(), transport,
                                  NULL, server_args);
      grpc_chttp2_transport_start_reading(&exec_ctx, transport, NULL);
    }

    /* create channel */
    {
      ChannelArguments args;
      args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, "test.authority");
      ApplyCommonChannelArguments(&args);

      grpc_channel_args c_args = args.c_channel_args();
      grpc_transport* transport =
          grpc_create_chttp2_transport(&exec_ctx, &c_args, endpoints.client, 1);
      GPR_ASSERT(transport);
      grpc_channel* channel = grpc_channel_create(
          &exec_ctx, "target", &c_args, GRPC_CLIENT_DIRECT_CHANNEL, transport);
      grpc_chttp2_transport_start_reading(&exec_ctx, transport, NULL);

      channel_ = CreateChannelInternal("", channel);
    }

    grpc_exec_ctx_finish(&exec_ctx);
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
  InProcessCHTTP2(Service* service)
      : EndpointPairFixture(service, MakeEndpoints()) {}

  int writes_performed() const { return stats_.num_writes; }

 private:
  grpc_passthru_endpoint_stats stats_;

  grpc_endpoint_pair MakeEndpoints() {
    grpc_endpoint_pair p;
    grpc_passthru_endpoint_create(&p.client, &p.server, initialize_stuff.rq(),
                                  &stats_);
    return p;
  }
};

static double UnaryPingPong(int request_size, int response_size) {
  const int kIterations = 10000;

  EchoTestService::AsyncService service;
  std::unique_ptr<InProcessCHTTP2> fixture(new InProcessCHTTP2(&service));
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
    GPR_ASSERT(fixture->cq()->Next(&t, &ok));
    GPR_ASSERT(ok);
    GPR_ASSERT(t == tag(0) || t == tag(1));
    intptr_t slot = reinterpret_cast<intptr_t>(t);
    ServerEnv* senv = server_env[slot];
    senv->response_writer.Finish(send_response, Status::OK, tag(3));
    response_reader->Finish(&recv_response, &recv_status, tag(4));
    for (int i = (1 << 3) | (1 << 4); i != 0;) {
      GPR_ASSERT(fixture->cq()->Next(&t, &ok));
      GPR_ASSERT(ok);
      int tagnum = (int)reinterpret_cast<intptr_t>(t);
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
      (double)fixture->writes_performed() / (double)kIterations;

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
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
