/*
 *
 * Copyright 2015 gRPC authors.
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

#include "test/cpp/qps/qps_worker.h"

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <grpc++/client_context.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/histogram.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/services.pb.h"
#include "test/core/util/grpc_profiler.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {

static std::unique_ptr<Client> CreateClient(const ClientConfig& config) {
  gpr_log(GPR_INFO, "Starting client of type %s %s %d",
          ClientType_Name(config.client_type()).c_str(),
          RpcType_Name(config.rpc_type()).c_str(),
          config.payload_config().has_bytebuf_params());

  switch (config.client_type()) {
    case ClientType::SYNC_CLIENT:
      return CreateSynchronousClient(config);
    case ClientType::ASYNC_CLIENT:
      return config.payload_config().has_bytebuf_params()
                 ? CreateGenericAsyncStreamingClient(config)
                 : CreateAsyncClient(config);
    default:
      abort();
  }
  abort();
}

static std::unique_ptr<Server> CreateServer(const ServerConfig& config) {
  gpr_log(GPR_INFO, "Starting server of type %s",
          ServerType_Name(config.server_type()).c_str());

  switch (config.server_type()) {
    case ServerType::SYNC_SERVER:
      return CreateSynchronousServer(config);
    case ServerType::ASYNC_SERVER:
      return CreateAsyncServer(config);
    case ServerType::ASYNC_GENERIC_SERVER:
      return CreateAsyncGenericServer(config);
    default:
      abort();
  }
  abort();
}

class ScopedProfile final {
 public:
  ScopedProfile(const char* filename, bool enable) : enable_(enable) {
    if (enable_) grpc_profiler_start(filename);
  }
  ~ScopedProfile() {
    if (enable_) grpc_profiler_stop();
  }

 private:
  const bool enable_;
};

class WorkerServiceImpl final : public WorkerService::Service {
 public:
  WorkerServiceImpl(int server_port, QpsWorker* worker)
      : acquired_(false), server_port_(server_port), worker_(worker) {}

  Status RunClient(
      ServerContext* ctx,
      ServerReaderWriter<ClientStatus, ClientArgs>* stream) override {
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(StatusCode::RESOURCE_EXHAUSTED, "Client worker busy");
    }

    ScopedProfile profile("qps_client.prof", false);
    Status ret = RunClientBody(ctx, stream);
    gpr_log(GPR_INFO, "RunClient: Returning");
    return ret;
  }

  Status RunServer(
      ServerContext* ctx,
      ServerReaderWriter<ServerStatus, ServerArgs>* stream) override {
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(StatusCode::RESOURCE_EXHAUSTED, "Server worker busy");
    }

    ScopedProfile profile("qps_server.prof", false);
    Status ret = RunServerBody(ctx, stream);
    gpr_log(GPR_INFO, "RunServer: Returning");
    return ret;
  }

  Status CoreCount(ServerContext* ctx, const CoreRequest*,
                   CoreResponse* resp) override {
    resp->set_cores(gpr_cpu_num_cores());
    return Status::OK;
  }

  Status QuitWorker(ServerContext* ctx, const Void*, Void*) override {
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(StatusCode::RESOURCE_EXHAUSTED, "Quitting worker busy");
    }

    worker_->MarkDone();
    return Status::OK;
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
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't read args");
    }
    if (!args.has_setup()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Invalid setup arg");
    }
    gpr_log(GPR_INFO, "RunClientBody: about to create client");
    auto client = CreateClient(args.setup());
    if (!client) {
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't create client");
    }
    gpr_log(GPR_INFO, "RunClientBody: client created");
    ClientStatus status;
    if (!stream->Write(status)) {
      return Status(StatusCode::UNKNOWN, "Client couldn't report init status");
    }
    gpr_log(GPR_INFO, "RunClientBody: creation status reported");
    while (stream->Read(&args)) {
      gpr_log(GPR_INFO, "RunClientBody: Message read");
      if (!args.has_mark()) {
        gpr_log(GPR_INFO, "RunClientBody: Message is not a mark!");
        return Status(StatusCode::INVALID_ARGUMENT, "Invalid mark");
      }
      *status.mutable_stats() = client->Mark(args.mark().reset());
      if (!stream->Write(status)) {
        return Status(StatusCode::UNKNOWN, "Client couldn't respond to mark");
      }
      gpr_log(GPR_INFO, "RunClientBody: Mark response given");
    }

    gpr_log(GPR_INFO, "RunClientBody: Awaiting Threads Completion");
    client->AwaitThreadsCompletion();

    gpr_log(GPR_INFO, "RunClientBody: Returning");
    return Status::OK;
  }

  Status RunServerBody(ServerContext* ctx,
                       ServerReaderWriter<ServerStatus, ServerArgs>* stream) {
    ServerArgs args;
    if (!stream->Read(&args)) {
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't read server args");
    }
    if (!args.has_setup()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Bad server creation args");
    }
    if (server_port_ != 0) {
      args.mutable_setup()->set_port(server_port_);
    }
    gpr_log(GPR_INFO, "RunServerBody: about to create server");
    auto server = CreateServer(args.setup());
    if (!server) {
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't create server");
    }
    gpr_log(GPR_INFO, "RunServerBody: server created");
    ServerStatus status;
    status.set_port(server->port());
    status.set_cores(server->cores());
    if (!stream->Write(status)) {
      return Status(StatusCode::UNKNOWN, "Server couldn't report init status");
    }
    gpr_log(GPR_INFO, "RunServerBody: creation status reported");
    while (stream->Read(&args)) {
      gpr_log(GPR_INFO, "RunServerBody: Message read");
      if (!args.has_mark()) {
        gpr_log(GPR_INFO, "RunServerBody: Message not a mark!");
        return Status(StatusCode::INVALID_ARGUMENT, "Invalid mark");
      }
      *status.mutable_stats() = server->Mark(args.mark().reset());
      if (!stream->Write(status)) {
        return Status(StatusCode::UNKNOWN, "Server couldn't respond to mark");
      }
      gpr_log(GPR_INFO, "RunServerBody: Mark response given");
    }

    gpr_log(GPR_INFO, "RunServerBody: Returning");
    return Status::OK;
  }

  std::mutex mu_;
  bool acquired_;
  int server_port_;
  QpsWorker* worker_;
};

QpsWorker::QpsWorker(int driver_port, int server_port,
                     const grpc::string& credential_type) {
  impl_.reset(new WorkerServiceImpl(server_port, this));
  gpr_atm_rel_store(&done_, static_cast<gpr_atm>(0));

  char* server_address = NULL;
  gpr_join_host_port(&server_address, "::", driver_port);

  ServerBuilder builder;
  builder.AddListeningPort(
      server_address,
      GetCredentialsProvider()->GetServerCredentials(credential_type));
  builder.RegisterService(impl_.get());

  gpr_free(server_address);

  server_ = builder.BuildAndStart();
}

QpsWorker::~QpsWorker() {}

bool QpsWorker::Done() const {
  return (gpr_atm_acq_load(&done_) != static_cast<gpr_atm>(0));
}
void QpsWorker::MarkDone() {
  gpr_atm_rel_store(&done_, static_cast<gpr_atm>(1));
}
}  // namespace testing
}  // namespace grpc
