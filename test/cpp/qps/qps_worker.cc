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

#include "test/cpp/qps/qps_worker.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/cpu.h>
#include <grpcpp/client_context.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "src/core/util/crash.h"
#include "src/core/util/host_port.h"
#include "src/proto/grpc/testing/worker_service.grpc.pb.h"
#include "test/core/test_util/grpc_profiler.h"
#include "test/core/test_util/histogram.h"
#include "test/cpp/qps/client.h"
#include "test/cpp/qps/qps_server_builder.h"
#include "test/cpp/qps/server.h"
#include "test/cpp/util/create_test_channel.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {

static std::unique_ptr<Client> CreateClient(const ClientConfig& config) {
  LOG(INFO) << "Starting client of type "
            << ClientType_Name(config.client_type()) << " "
            << RpcType_Name(config.rpc_type()) << " "
            << config.payload_config().has_bytebuf_params();

  switch (config.client_type()) {
    case ClientType::SYNC_CLIENT:
      return CreateSynchronousClient(config);
    case ClientType::ASYNC_CLIENT:
      return config.payload_config().has_bytebuf_params()
                 ? CreateGenericAsyncStreamingClient(config)
                 : CreateAsyncClient(config);
    case ClientType::CALLBACK_CLIENT:
      return CreateCallbackClient(config);
    default:
      abort();
  }
}

static std::unique_ptr<Server> CreateServer(const ServerConfig& config) {
  LOG(INFO) << "Starting server of type "
            << ServerType_Name(config.server_type());

  switch (config.server_type()) {
    case ServerType::SYNC_SERVER:
      return CreateSynchronousServer(config);
    case ServerType::ASYNC_SERVER:
      return CreateAsyncServer(config);
    case ServerType::ASYNC_GENERIC_SERVER:
      return CreateAsyncGenericServer(config);
    case ServerType::CALLBACK_SERVER:
      return CreateCallbackServer(config);
    default:
      abort();
  }
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
    LOG(INFO) << "RunClient: Entering";
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(StatusCode::RESOURCE_EXHAUSTED, "Client worker busy");
    }

    ScopedProfile profile("qps_client.prof", false);
    Status ret = RunClientBody(ctx, stream);
    LOG(INFO) << "RunClient: Returning";
    return ret;
  }

  Status RunServer(
      ServerContext* ctx,
      ServerReaderWriter<ServerStatus, ServerArgs>* stream) override {
    LOG(INFO) << "RunServer: Entering";
    InstanceGuard g(this);
    if (!g.Acquired()) {
      return Status(StatusCode::RESOURCE_EXHAUSTED, "Server worker busy");
    }

    ScopedProfile profile("qps_server.prof", false);
    Status ret = RunServerBody(ctx, stream);
    LOG(INFO) << "RunServer: Returning";
    return ret;
  }

  Status CoreCount(ServerContext* /*ctx*/, const CoreRequest*,
                   CoreResponse* resp) override {
    resp->set_cores(gpr_cpu_num_cores());
    return Status::OK;
  }

  Status QuitWorker(ServerContext* /*ctx*/, const Void*, Void*) override {
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
    explicit InstanceGuard(WorkerServiceImpl* impl)
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
    CHECK(acquired_);
    acquired_ = false;
  }

  Status RunClientBody(ServerContext* /*ctx*/,
                       ServerReaderWriter<ClientStatus, ClientArgs>* stream) {
    ClientArgs args;
    if (!stream->Read(&args)) {
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't read args");
    }
    if (!args.has_setup()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Invalid setup arg");
    }
    LOG(INFO) << "RunClientBody: about to create client";
    std::unique_ptr<Client> client = CreateClient(args.setup());
    if (!client) {
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't create client");
    }
    LOG(INFO) << "RunClientBody: client created";
    ClientStatus status;
    if (!stream->Write(status)) {
      return Status(StatusCode::UNKNOWN, "Client couldn't report init status");
    }
    LOG(INFO) << "RunClientBody: creation status reported";
    while (stream->Read(&args)) {
      LOG(INFO) << "RunClientBody: Message read";
      if (!args.has_mark()) {
        LOG(INFO) << "RunClientBody: Message is not a mark!";
        return Status(StatusCode::INVALID_ARGUMENT, "Invalid mark");
      }
      *status.mutable_stats() = client->Mark(args.mark().reset());
      if (!stream->Write(status)) {
        return Status(StatusCode::UNKNOWN, "Client couldn't respond to mark");
      }
      LOG(INFO) << "RunClientBody: Mark response given";
    }

    LOG(INFO) << "RunClientBody: Awaiting Threads Completion";
    client->AwaitThreadsCompletion();

    LOG(INFO) << "RunClientBody: Returning";
    return Status::OK;
  }

  Status RunServerBody(ServerContext* /*ctx*/,
                       ServerReaderWriter<ServerStatus, ServerArgs>* stream) {
    ServerArgs args;
    if (!stream->Read(&args)) {
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't read server args");
    }
    if (!args.has_setup()) {
      return Status(StatusCode::INVALID_ARGUMENT, "Bad server creation args");
    }
    if (server_port_ > 0 && args.setup().port() == 0) {
      args.mutable_setup()->set_port(server_port_);
    }
    LOG(INFO) << "RunServerBody: about to create server";
    std::unique_ptr<Server> server = CreateServer(args.setup());
    if (g_inproc_servers != nullptr) {
      g_inproc_servers->push_back(server.get());
    }
    if (!server) {
      return Status(StatusCode::INVALID_ARGUMENT, "Couldn't create server");
    }
    LOG(INFO) << "RunServerBody: server created";
    ServerStatus status;
    status.set_port(server->port());
    status.set_cores(server->cores());
    if (!stream->Write(status)) {
      return Status(StatusCode::UNKNOWN, "Server couldn't report init status");
    }
    LOG(INFO) << "RunServerBody: creation status reported";
    while (stream->Read(&args)) {
      LOG(INFO) << "RunServerBody: Message read";
      if (!args.has_mark()) {
        LOG(INFO) << "RunServerBody: Message not a mark!";
        return Status(StatusCode::INVALID_ARGUMENT, "Invalid mark");
      }
      *status.mutable_stats() = server->Mark(args.mark().reset());
      if (!stream->Write(status)) {
        return Status(StatusCode::UNKNOWN, "Server couldn't respond to mark");
      }
      LOG(INFO) << "RunServerBody: Mark response given";
    }

    LOG(INFO) << "RunServerBody: Returning";
    return Status::OK;
  }

  std::mutex mu_;
  bool acquired_;
  int server_port_;
  QpsWorker* worker_;
};

QpsWorker::QpsWorker(int driver_port, int server_port,
                     const std::string& credential_type) {
  impl_ = std::make_unique<WorkerServiceImpl>(server_port, this);
  gpr_atm_rel_store(&done_, gpr_atm{0});

  std::unique_ptr<ServerBuilder> builder = CreateQpsServerBuilder();
  builder->AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  if (driver_port >= 0) {
    std::string server_address = grpc_core::JoinHostPort("::", driver_port);
    builder->AddListeningPort(
        server_address,
        GetCredentialsProvider()->GetServerCredentials(credential_type));
  }
  builder->RegisterService(impl_.get());

  server_ = builder->BuildAndStart();
  if (server_ == nullptr) {
    LOG(ERROR) << "QpsWorker: Fail to BuildAndStart(driver_port=" << driver_port
               << ", server_port=" << server_port << ")";
  } else {
    LOG(INFO) << "QpsWorker: BuildAndStart(driver_port=" << driver_port
              << ", server_port=" << server_port << ") done";
  }
}

QpsWorker::~QpsWorker() {}

bool QpsWorker::Done() const {
  return (gpr_atm_acq_load(&done_) != gpr_atm{0});
}
void QpsWorker::MarkDone() { gpr_atm_rel_store(&done_, gpr_atm{1}); }
}  // namespace testing
}  // namespace grpc
