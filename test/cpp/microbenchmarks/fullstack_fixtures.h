/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef TEST_CPP_MICROBENCHMARKS_FULLSTACK_FIXTURES_H
#define TEST_CPP_MICROBENCHMARKS_FULLSTACK_FIXTURES_H

#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
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

#include "test/cpp/microbenchmarks/helpers.h"

namespace grpc {
namespace testing {

static void ApplyCommonServerBuilderConfig(ServerBuilder* b) {
  b->SetMaxReceiveMessageSize(INT_MAX);
  b->SetMaxSendMessageSize(INT_MAX);
}

static void ApplyCommonChannelArguments(ChannelArguments* c) {
  c->SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, INT_MAX);
  c->SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, INT_MAX);
}

class BaseFixture : public TrackCounters {};

class FullstackFixture : public BaseFixture {
 public:
  FullstackFixture(Service* service, const grpc::string& address) {
    ServerBuilder b;
    b.AddListeningPort(address, InsecureServerCredentials());
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();
    ChannelArguments args;
    ApplyCommonChannelArguments(&args);
    channel_ = CreateCustomChannel(address, InsecureChannelCredentials(), args);
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
  UDS(Service* service) : FullstackFixture(service, MakeAddress()) {}

 private:
  static grpc::string MakeAddress() {
    int port = grpc_pick_unused_port_or_die();  // just for a unique id - not a
                                                // real port
    std::stringstream addr;
    addr << "unix:/tmp/bm_fullstack." << port;
    return addr.str();
  }
};

class EndpointPairFixture : public BaseFixture {
 public:
  EndpointPairFixture(Service* service, grpc_endpoint_pair endpoints)
      : endpoint_pair_(endpoints) {
    ServerBuilder b;
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();

    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

    /* add server endpoint to server_
     * */
    {
      const grpc_channel_args* server_args =
          grpc_server_get_channel_args(server_->c_server());
      server_transport_ = grpc_create_chttp2_transport(
          &exec_ctx, server_args, endpoints.server, 0 /* is_client */);

      grpc_pollset** pollsets;
      size_t num_pollsets = 0;
      grpc_server_get_pollsets(server_->c_server(), &pollsets, &num_pollsets);

      for (size_t i = 0; i < num_pollsets; i++) {
        grpc_endpoint_add_to_pollset(&exec_ctx, endpoints.server, pollsets[i]);
      }

      grpc_server_setup_transport(&exec_ctx, server_->c_server(),
                                  server_transport_, NULL, server_args);
      grpc_chttp2_transport_start_reading(&exec_ctx, server_transport_, NULL);
    }

    /* create channel */
    {
      ChannelArguments args;
      args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, "test.authority");
      ApplyCommonChannelArguments(&args);

      grpc_channel_args c_args = args.c_channel_args();
      client_transport_ =
          grpc_create_chttp2_transport(&exec_ctx, &c_args, endpoints.client, 1);
      GPR_ASSERT(client_transport_);
      grpc_channel* channel =
          grpc_channel_create(&exec_ctx, "target", &c_args,
                              GRPC_CLIENT_DIRECT_CHANNEL, client_transport_);
      grpc_chttp2_transport_start_reading(&exec_ctx, client_transport_, NULL);

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

 protected:
  grpc_endpoint_pair endpoint_pair_;
  grpc_transport* client_transport_;
  grpc_transport* server_transport_;

 private:
  std::unique_ptr<Server> server_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::shared_ptr<Channel> channel_;
};

class SockPair : public EndpointPairFixture {
 public:
  SockPair(Service* service)
      : EndpointPairFixture(service,
                            grpc_iomgr_create_endpoint_pair("test", NULL)) {}
};

class InProcessCHTTP2 : public EndpointPairFixture {
 public:
  InProcessCHTTP2(Service* service)
      : EndpointPairFixture(service, MakeEndpoints()) {}

  void AddToLabel(std::ostream& out, benchmark::State& state) {
    EndpointPairFixture::AddToLabel(out, state);
    out << " writes/iter:"
        << ((double)stats_.num_writes / (double)state.iterations());
  }

 private:
  grpc_passthru_endpoint_stats stats_;

  grpc_endpoint_pair MakeEndpoints() {
    grpc_endpoint_pair p;
    grpc_passthru_endpoint_create(&p.client, &p.server, Library::get().rq(),
                                  &stats_);
    return p;
  }
};

}  // namespace testing
}  // namespace grpc

#endif
