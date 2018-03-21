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

#ifndef TEST_CPP_MICROBENCHMARKS_FULLSTACK_FIXTURES_H
#define TEST_CPP_MICROBENCHMARKS_FULLSTACK_FIXTURES_H

#include <grpc/support/atm.h>
#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

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

#include "src/cpp/client/create_channel_internal.h"
#include "test/cpp/microbenchmarks/helpers.h"

namespace grpc {
namespace testing {

class FixtureConfiguration {
 public:
  virtual void ApplyCommonChannelArguments(ChannelArguments* c) const {
    c->SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, INT_MAX);
    c->SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, INT_MAX);
  }

  virtual void ApplyCommonServerBuilderConfig(ServerBuilder* b) const {
    b->SetMaxReceiveMessageSize(INT_MAX);
    b->SetMaxSendMessageSize(INT_MAX);
  }
};

class BaseFixture : public TrackCounters {};

// Special tag to be used in Server shutdown. This tag is *NEVER* returned when
// Cq->Next() API is called (This is because FinalizeResult() function in this
// class always returns 'false'). This is intentional and makes writing shutdown
// code easier.
class ShutdownTag : public internal::CompletionQueueTag {
 public:
  bool FinalizeResult(void** tag, bool* status) { return false; }
};

class FullstackFixture : public BaseFixture {
 public:
  FullstackFixture(Service* service, const FixtureConfiguration& config,
                   const grpc::string& address) {
    ServerBuilder b;
    if (address.length() > 0) {
      b.AddListeningPort(address, InsecureServerCredentials());
    }
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    config.ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();
    ChannelArguments args;
    config.ApplyCommonChannelArguments(&args);
    if (address.length() > 0) {
      channel_ =
          CreateCustomChannel(address, InsecureChannelCredentials(), args);
    } else {
      channel_ = server_->InProcessChannel(args);
    }
  }

  virtual ~FullstackFixture() {
    // Dummy shutdown tag (this tag is swallowed by cq->Next() and is not
    // returned to the user) see ShutdownTag definition for more details
    ShutdownTag shutdown_tag;
    grpc_server_shutdown_and_notify(server_->c_server(), cq_->cq(),
                                    &shutdown_tag);
    cq_->Shutdown();
    void* tag;
    bool ok;
    while (cq_->Next(&tag, &ok)) {
    }
  }

  void AddToLabel(std::ostream& out, benchmark::State& state) {
    BaseFixture::AddToLabel(out, state);
    out << " polls/iter:"
        << static_cast<double>(grpc_get_cq_poll_num(this->cq()->cq())) /
               state.iterations();
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
  TCP(Service* service, const FixtureConfiguration& fixture_configuration =
                            FixtureConfiguration())
      : FullstackFixture(service, fixture_configuration, MakeAddress(&port_)) {}

  ~TCP() { grpc_recycle_unused_port(port_); }

 private:
  int port_;

  static grpc::string MakeAddress(int* port) {
    *port = grpc_pick_unused_port_or_die();
    std::stringstream addr;
    addr << "localhost:" << *port;
    return addr.str();
  }
};

class UDS : public FullstackFixture {
 public:
  UDS(Service* service, const FixtureConfiguration& fixture_configuration =
                            FixtureConfiguration())
      : FullstackFixture(service, fixture_configuration, MakeAddress(&port_)) {}

  ~UDS() { grpc_recycle_unused_port(port_); }

 private:
  int port_;

  static grpc::string MakeAddress(int* port) {
    *port = grpc_pick_unused_port_or_die();  // just for a unique id - not a
                                             // real port
    std::stringstream addr;
    addr << "unix:/tmp/bm_fullstack." << *port;
    return addr.str();
  }
};

class InProcess : public FullstackFixture {
 public:
  InProcess(Service* service,
            const FixtureConfiguration& fixture_configuration =
                FixtureConfiguration())
      : FullstackFixture(service, fixture_configuration, "") {}
  ~InProcess() {}
};

class EndpointPairFixture : public BaseFixture {
 public:
  EndpointPairFixture(Service* service, grpc_endpoint_pair endpoints,
                      const FixtureConfiguration& fixture_configuration)
      : endpoint_pair_(endpoints) {
    ServerBuilder b;
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    fixture_configuration.ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();

    grpc_core::ExecCtx exec_ctx;

    /* add server endpoint to server_
     * */
    {
      const grpc_channel_args* server_args =
          grpc_server_get_channel_args(server_->c_server());
      server_transport_ = grpc_create_chttp2_transport(
          server_args, endpoints.server, false /* is_client */);

      grpc_pollset** pollsets;
      size_t num_pollsets = 0;
      grpc_server_get_pollsets(server_->c_server(), &pollsets, &num_pollsets);

      for (size_t i = 0; i < num_pollsets; i++) {
        grpc_endpoint_add_to_pollset(endpoints.server, pollsets[i]);
      }

      grpc_server_setup_transport(server_->c_server(), server_transport_,
                                  nullptr, server_args);
      grpc_chttp2_transport_start_reading(server_transport_, nullptr, nullptr);
    }

    /* create channel */
    {
      ChannelArguments args;
      args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, "test.authority");
      fixture_configuration.ApplyCommonChannelArguments(&args);

      grpc_channel_args c_args = args.c_channel_args();
      client_transport_ =
          grpc_create_chttp2_transport(&c_args, endpoints.client, true);
      GPR_ASSERT(client_transport_);
      grpc_channel* channel = grpc_channel_create(
          "target", &c_args, GRPC_CLIENT_DIRECT_CHANNEL, client_transport_);
      grpc_chttp2_transport_start_reading(client_transport_, nullptr, nullptr);

      channel_ = CreateChannelInternal("", channel);
    }
  }

  virtual ~EndpointPairFixture() {
    // Dummy shutdown tag (this tag is swallowed by cq->Next() and is not
    // returned to the user) see ShutdownTag definition for more details
    ShutdownTag shutdown_tag;
    grpc_server_shutdown_and_notify(server_->c_server(), cq_->cq(),
                                    &shutdown_tag);
    cq_->Shutdown();
    void* tag;
    bool ok;
    while (cq_->Next(&tag, &ok)) {
    }
  }

  void AddToLabel(std::ostream& out, benchmark::State& state) {
    BaseFixture::AddToLabel(out, state);
    out << " polls/iter:"
        << static_cast<double>(grpc_get_cq_poll_num(this->cq()->cq())) /
               state.iterations();
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
  SockPair(Service* service, const FixtureConfiguration& fixture_configuration =
                                 FixtureConfiguration())
      : EndpointPairFixture(service,
                            grpc_iomgr_create_endpoint_pair("test", nullptr),
                            fixture_configuration) {}
};

/* Use InProcessCHTTP2 instead. This class (with stats as an explicit parameter)
   is here only to be able to initialize both the base class and stats_ with the
   same stats instance without accessing the stats_ fields before the object is
   properly initialized. */
class InProcessCHTTP2WithExplicitStats : public EndpointPairFixture {
 public:
  InProcessCHTTP2WithExplicitStats(
      Service* service, grpc_passthru_endpoint_stats* stats,
      const FixtureConfiguration& fixture_configuration)
      : EndpointPairFixture(service, MakeEndpoints(stats),
                            fixture_configuration),
        stats_(stats) {}

  virtual ~InProcessCHTTP2WithExplicitStats() {
    if (stats_ != nullptr) {
      grpc_passthru_endpoint_stats_destroy(stats_);
    }
  }

  void AddToLabel(std::ostream& out, benchmark::State& state) {
    EndpointPairFixture::AddToLabel(out, state);
    out << " writes/iter:"
        << static_cast<double>(gpr_atm_no_barrier_load(&stats_->num_writes)) /
               static_cast<double>(state.iterations());
  }

 private:
  grpc_passthru_endpoint_stats* stats_;

  static grpc_endpoint_pair MakeEndpoints(grpc_passthru_endpoint_stats* stats) {
    grpc_endpoint_pair p;
    grpc_passthru_endpoint_create(&p.client, &p.server, Library::get().rq(),
                                  stats);
    return p;
  }
};

class InProcessCHTTP2 : public InProcessCHTTP2WithExplicitStats {
 public:
  InProcessCHTTP2(Service* service,
                  const FixtureConfiguration& fixture_configuration =
                      FixtureConfiguration())
      : InProcessCHTTP2WithExplicitStats(service,
                                         grpc_passthru_endpoint_stats_create(),
                                         fixture_configuration) {}
};

////////////////////////////////////////////////////////////////////////////////
// Minimal stack fixtures

class MinStackConfiguration : public FixtureConfiguration {
  void ApplyCommonChannelArguments(ChannelArguments* a) const override {
    a->SetInt(GRPC_ARG_MINIMAL_STACK, 1);
    FixtureConfiguration::ApplyCommonChannelArguments(a);
  }

  void ApplyCommonServerBuilderConfig(ServerBuilder* b) const override {
    b->AddChannelArgument(GRPC_ARG_MINIMAL_STACK, 1);
    FixtureConfiguration::ApplyCommonServerBuilderConfig(b);
  }
};

template <class Base>
class MinStackize : public Base {
 public:
  MinStackize(Service* service) : Base(service, MinStackConfiguration()) {}
};

typedef MinStackize<TCP> MinTCP;
typedef MinStackize<UDS> MinUDS;
typedef MinStackize<InProcess> MinInProcess;
typedef MinStackize<SockPair> MinSockPair;
typedef MinStackize<InProcessCHTTP2> MinInProcessCHTTP2;

}  // namespace testing
}  // namespace grpc

#endif
