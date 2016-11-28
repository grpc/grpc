/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Benchmark gRPC end2end in various configurations */

#include <sstream>

#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/impl/grpc_library.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/support/log.h>

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
#include "third_party/google_benchmark/include/benchmark/benchmark.h"

namespace grpc {
namespace testing {

static class InitializeStuff {
 public:
  InitializeStuff() {
    init_lib_.init();
    rq_ = grpc_resource_quota_create("bm");
  }

  grpc_resource_quota* rq() { return rq_; }

 private:
  internal::GrpcLibrary init_lib_;
  grpc_resource_quota* rq_;
} initialize_stuff;

/*******************************************************************************
 * FIXTURES
 */

class FullstackFixture {
 public:
  FullstackFixture(Service* service, const grpc::string& address) {
    ServerBuilder b;
    b.AddListeningPort(address, InsecureServerCredentials());
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    server_ = b.BuildAndStart();
    channel_ = CreateChannel(address, InsecureChannelCredentials());
  }

  virtual ~FullstackFixture() {
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

class TCP : public FullstackFixture {
 public:
  TCP(Service* service) : FullstackFixture(service, MakeAddress()) {}

 private:
  static grpc::string MakeAddress() {
    int port = grpc_pick_unused_port_or_die();
    std::stringstream addr;
    addr << "localhost:" << port;
    return addr.str();
  }
};

class UDS : public FullstackFixture {
 public:
  UDS(Service* service) : FullstackFixture(service, "unix:bm_fullstack") {}
};

class EndpointPairFixture {
 public:
  EndpointPairFixture(Service* service, grpc_endpoint_pair endpoints) {
    ServerBuilder b;
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
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

class SockPair : public EndpointPairFixture {
 public:
  SockPair(Service* service)
      : EndpointPairFixture(service, grpc_iomgr_create_endpoint_pair(
                                         "test", initialize_stuff.rq(), 8192)) {
  }
};

class InProcessCHTTP2 : public EndpointPairFixture {
 public:
  InProcessCHTTP2(Service* service)
      : EndpointPairFixture(service, MakeEndpoints()) {}

 private:
  grpc_endpoint_pair MakeEndpoints() {
    grpc_endpoint_pair p;
    grpc_passthru_endpoint_create(&p.client, &p.server, initialize_stuff.rq());
    return p;
  }
};

/*******************************************************************************
 * CONTEXT MUTATORS
 */

static const int kPregenerateKeyCount = 100000;

template <class F>
auto MakeVector(size_t length, F f) -> std::vector<decltype(f())> {
  std::vector<decltype(f())> out;
  out.reserve(length);
  for (size_t i = 0; i < length; i++) {
    out.push_back(f());
  }
  return out;
}

class NoOpMutator {
 public:
  template <class ContextType>
  NoOpMutator(ContextType* context) {}
};

template <int length>
class RandomBinaryMetadata {
 public:
  static const grpc::string& Key() { return kKey; }

  static const grpc::string& Value() {
    return kValues[rand() % kValues.size()];
  }

 private:
  static const grpc::string kKey;
  static const std::vector<grpc::string> kValues;

  static grpc::string GenerateOneString() {
    grpc::string s;
    s.reserve(length + 1);
    for (int i = 0; i < length; i++) {
      s += (char)rand();
    }
    return s;
  }
};

template <int length>
const grpc::string RandomBinaryMetadata<length>::kKey = "foo-bin";

template <int length>
const std::vector<grpc::string> RandomBinaryMetadata<length>::kValues =
    MakeVector(kPregenerateKeyCount, GenerateOneString);

template <int length>
class RandomAsciiMetadata {
 public:
  static const grpc::string& Key() { return kKey; }

  static const grpc::string& Value() {
    return kValues[rand() % kValues.size()];
  }

 private:
  static const grpc::string kKey;
  static const std::vector<grpc::string> kValues;

  static grpc::string GenerateOneString() {
    grpc::string s;
    s.reserve(length + 1);
    for (int i = 0; i < length; i++) {
      s += (char)(rand() % 26 + 'a');
    }
    return s;
  }
};

template <int length>
const grpc::string RandomAsciiMetadata<length>::kKey = "foo";

template <int length>
const std::vector<grpc::string> RandomAsciiMetadata<length>::kValues =
    MakeVector(kPregenerateKeyCount, GenerateOneString);

template <class Generator, int kNumKeys>
class Client_AddMetadata : public NoOpMutator {
 public:
  Client_AddMetadata(ClientContext* context) : NoOpMutator(context) {
    for (int i = 0; i < kNumKeys; i++) {
      context->AddMetadata(Generator::Key(), Generator::Value());
    }
  }
};

template <class Generator, int kNumKeys>
class Server_AddInitialMetadata : public NoOpMutator {
 public:
  Server_AddInitialMetadata(ServerContext* context) : NoOpMutator(context) {
    for (int i = 0; i < kNumKeys; i++) {
      context->AddInitialMetadata(Generator::Key(), Generator::Value());
    }
  }
};

/*******************************************************************************
 * BENCHMARKING KERNELS
 */

static void* tag(intptr_t x) { return reinterpret_cast<void*>(x); }

template <class Fixture, class ClientContextMutator, class ServerContextMutator>
static void BM_UnaryPingPong(benchmark::State& state) {
  EchoTestService::AsyncService service;
  std::unique_ptr<Fixture> fixture(new Fixture(&service));
  EchoRequest send_request;
  EchoResponse send_response;
  EchoResponse recv_response;
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
  while (state.KeepRunning()) {
    ClientContext cli_ctx;
    ClientContextMutator cli_ctx_mut(&cli_ctx);
    std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader(
        stub->AsyncEcho(&cli_ctx, send_request, fixture->cq()));
    void* t;
    bool ok;
    GPR_ASSERT(fixture->cq()->Next(&t, &ok));
    GPR_ASSERT(ok);
    GPR_ASSERT(t == tag(0) || t == tag(1));
    intptr_t slot = reinterpret_cast<intptr_t>(t);
    ServerEnv* senv = server_env[slot];
    ServerContextMutator svr_ctx_mut(&senv->ctx);
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
  fixture.reset();
  server_env[0]->~ServerEnv();
  server_env[1]->~ServerEnv();
}

/*******************************************************************************
 * CONFIGURATIONS
 */

BENCHMARK_TEMPLATE(BM_UnaryPingPong, TCP, NoOpMutator, NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, UDS, NoOpMutator, NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, SockPair, NoOpMutator, NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator, NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 1>,
                   NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 1>,
                   NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 1>,
                   NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 2>,
                   NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 2>,
                   NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 2>,
                   NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<10>, 1>);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<31>, 1>);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<100>, 1>);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<10>, 1>, NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<31>, 1>, NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<100>, 1>,
                   NoOpMutator);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 1>);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<31>, 1>);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<100>, 1>);

}  // namespace testing
}  // namespace grpc

BENCHMARK_MAIN();
