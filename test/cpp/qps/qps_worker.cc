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

#include "test/cpp/qps/qps_worker.h"

#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sstream>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/histogram.h>
#include <grpc/support/log.h>
#include <grpc/support/host_port.h>
#include <grpc++/client_context.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/security/server_credentials.h>

#include "test/core/util/grpc_profiler.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/proto/benchmarks/services.pb.h"

namespace grpc {
namespace testing {

static std::unique_ptr<Client> CreateClient(const ClientConfig& config) {
  switch (config.client_type()) {
    case ClientType::SYNC_CLIENT:
      return (config.rpc_type() == RpcType::UNARY)
                 ? CreateSynchronousUnaryClient(config)
                 : CreateSynchronousStreamingClient(config);
    case ClientType::ASYNC_CLIENT:
      return (config.rpc_type() == RpcType::UNARY)
                 ? CreateAsyncUnaryClient(config)
                 : CreateAsyncStreamingClient(config);
    default:
      abort();
  }
  abort();
}

static void LimitCores(int cores) {}

static std::unique_ptr<Server> CreateServer(const ServerConfig& config) {
  if (config.core_limit() > 0) {
    LimitCores(config.core_limit());
  }
  switch (config.server_type()) {
    case ServerType::SYNC_SERVER:
      return CreateSynchronousServer(config);
    case ServerType::ASYNC_SERVER:
      return CreateAsyncServer(config);
    default:
      abort();
  }
  abort();
}

class WorkerServiceImpl GRPC_FINAL : public WorkerService::Service {
 public:
  explicit WorkerServiceImpl() : acquired_(false) {}

  Status RunClient(ServerContext* ctx,
                   ServerReaderWriter<ClientStatus, ClientArgs>* stream)
      GRPC_OVERRIDE {
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(StatusCode::RESOURCE_EXHAUSTED, "");
    }

    grpc_profiler_start("qps_client.prof");
    Status ret = RunClientBody(ctx, stream);
    grpc_profiler_stop();
    return ret;
  }

  Status RunServer(ServerContext* ctx,
                   ServerReaderWriter<ServerStatus, ServerArgs>* stream)
      GRPC_OVERRIDE {
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(StatusCode::RESOURCE_EXHAUSTED, "");
    }

    grpc_profiler_start("qps_server.prof");
    Status ret = RunServerBody(ctx, stream);
    grpc_profiler_stop();
    return ret;
  }

 private:
  // Protect against multiple clients using this worker at once.
  class InstanceGuard {
   public:
    InstanceGuard(WorkerServiceImpl* impl)
        : impl_(impl), acquired_(impl->TryAcquireInstance()) {}
    ~InstanceGuard() {
      if (acquired_) {
        impl_->ReleaseInstance();
      }
    }

    bool Acquired() const { return acquired_; }

   private:
    WorkerServiceImpl* const impl_;
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

  Status RunClientBody(ServerContext* ctx,
                       ServerReaderWriter<ClientStatus, ClientArgs>* stream) {
    ClientArgs args;
    if (!stream->Read(&args)) {
      return Status(StatusCode::INVALID_ARGUMENT, "");
    }
    if (!args.has_setup()) {
      return Status(StatusCode::INVALID_ARGUMENT, "");
    }
    auto client = CreateClient(args.setup());
    if (!client) {
      return Status(StatusCode::INVALID_ARGUMENT, "");
    }
    ClientStatus status;
    if (!stream->Write(status)) {
      return Status(StatusCode::UNKNOWN, "");
    }
    while (stream->Read(&args)) {
      if (!args.has_mark()) {
        return Status(StatusCode::INVALID_ARGUMENT, "");
      }
      *status.mutable_stats() = client->Mark(args.mark().reset());
      stream->Write(status);
    }

    return Status::OK;
  }

  Status RunServerBody(ServerContext* ctx,
                       ServerReaderWriter<ServerStatus, ServerArgs>* stream) {
    ServerArgs args;
    if (!stream->Read(&args)) {
      return Status(StatusCode::INVALID_ARGUMENT, "");
    }
    if (!args.has_setup()) {
      return Status(StatusCode::INVALID_ARGUMENT, "");
    }
    auto server = CreateServer(args.setup());
    if (!server) {
      return Status(StatusCode::INVALID_ARGUMENT, "");
    }
    ServerStatus status;
    status.set_port(server->port());
    status.set_cores(server->cores());
    if (!stream->Write(status)) {
      return Status(StatusCode::UNKNOWN, "");
    }
    while (stream->Read(&args)) {
      if (!args.has_mark()) {
        return Status(StatusCode::INVALID_ARGUMENT, "");
      }
      *status.mutable_stats() = server->Mark(args.mark().reset());
      stream->Write(status);
    }

    return Status::OK;
  }

  std::mutex mu_;
  bool acquired_;
};

QpsWorker::QpsWorker(int driver_port) {
  impl_.reset(new WorkerServiceImpl());

  char* server_address = NULL;
  gpr_join_host_port(&server_address, "::", driver_port);

  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(impl_.get());

  gpr_free(server_address);

  server_ = builder.BuildAndStart();
}

QpsWorker::~QpsWorker() {}

}  // namespace testing
}  // namespace grpc
