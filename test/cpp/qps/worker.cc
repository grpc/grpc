/*
 *
 * Copyright 2015, Google Inc.
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

#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sstream>

#include <sys/signal.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/histogram.h>
#include <grpc/support/log.h>
#include <grpc/support/host_port.h>
#include <gflags/gflags.h>
#include <grpc++/client_context.h>
#include <grpc++/status.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_credentials.h>
#include <grpc++/stream.h>
#include "test/core/util/grpc_profiler.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/qps/qpstest.pb.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/server.h"

DEFINE_int32(driver_port, 0, "Driver server port.");
DEFINE_int32(server_port, 0, "Spawned server port.");

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

static bool got_sigint = false;

namespace grpc {
namespace testing {

std::unique_ptr<Client> CreateClient(const ClientConfig& config) {
  switch (config.client_type()) {
    case ClientType::SYNCHRONOUS_CLIENT:
      return CreateSynchronousClient(config);
    case ClientType::ASYNC_CLIENT:
      return CreateAsyncClient(config);
  }
  abort();
}

std::unique_ptr<Server> CreateServer(const ServerConfig& config) {
  switch (config.server_type()) {
    case ServerType::SYNCHRONOUS_SERVER:
      return CreateSynchronousServer(config, FLAGS_server_port);
    case ServerType::ASYNC_SERVER:
      return CreateAsyncServer(config, FLAGS_server_port);
  }
  abort();
}

class WorkerImpl GRPC_FINAL : public Worker::Service {
 public:
  WorkerImpl() : acquired_(false) {}

  Status RunTest(ServerContext* ctx,
                 ServerReaderWriter<ClientStatus, ClientArgs>* stream)
      GRPC_OVERRIDE {
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(RESOURCE_EXHAUSTED);
    }

    ClientArgs args;
    if (!stream->Read(&args)) {
      return Status(INVALID_ARGUMENT);
    }
    if (!args.has_setup()) {
      return Status(INVALID_ARGUMENT);
    }
    auto client = CreateClient(args.setup());
    if (!client) {
      return Status(INVALID_ARGUMENT);
    }
    ClientStatus status;
    if (!stream->Write(status)) {
      return Status(UNKNOWN);
    }
    while (stream->Read(&args)) {
      if (!args.has_mark()) {
        return Status(INVALID_ARGUMENT);
      }
      *status.mutable_stats() = client->Mark();
      stream->Write(status);
    }

    return Status::OK;
  }

  Status RunServer(ServerContext* ctx,
                   ServerReaderWriter<ServerStatus, ServerArgs>* stream)
      GRPC_OVERRIDE {
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(RESOURCE_EXHAUSTED);
    }

    ServerArgs args;
    if (!stream->Read(&args)) {
      return Status(INVALID_ARGUMENT);
    }
    if (!args.has_setup()) {
      return Status(INVALID_ARGUMENT);
    }
    auto server = CreateServer(args.setup());
    if (!server) {
      return Status(INVALID_ARGUMENT);
    }
    ServerStatus status;
    status.set_port(FLAGS_server_port);
    if (!stream->Write(status)) {
      return Status(UNKNOWN);
    }
    while (stream->Read(&args)) {
      if (!args.has_mark()) {
        return Status(INVALID_ARGUMENT);
      }
      *status.mutable_stats() = server->Mark();
      stream->Write(status);
    }

    return Status::OK;
  }

 private:
  // Protect against multiple clients using this worker at once.
  class InstanceGuard {
   public:
    InstanceGuard(WorkerImpl* impl)
        : impl_(impl), acquired_(impl->TryAcquireInstance()) {}
    ~InstanceGuard() {
      if (acquired_) {
        impl_->ReleaseInstance();
      }
    }

    bool Acquired() const { return acquired_; }

   private:
    WorkerImpl* const impl_;
    const bool acquired_;
  };

  bool TryAcquireInstance() {
    std::lock_guard<std::mutex> g(mu_);
    if (acquired_) return false;
    acquired_ = true;
    return true;
  }

  void ReleaseInstance() {
    std::lock_guard<std::mutex> g(mu_);
    GPR_ASSERT(acquired_);
    acquired_ = false;
  }

  std::mutex mu_;
  bool acquired_;
};

static void RunServer() {
  char* server_address = NULL;
  gpr_join_host_port(&server_address, "::", FLAGS_driver_port);

  WorkerImpl service;

  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);

  gpr_free(server_address);

  auto server = builder.BuildAndStart();

  while (!got_sigint) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  ParseCommandLineFlags(&argc, &argv, true);

  grpc::testing::RunServer();

  grpc_shutdown();
  return 0;
}
