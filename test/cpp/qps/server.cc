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

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <thread>

#include <gflags/gflags.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc++/config.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "src/cpp/server/thread_pool.h"
#include "test/core/util/grpc_profiler.h"
#include "test/cpp/qps/qpstest.pb.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

DEFINE_int32(port, 0, "Server port.");
DEFINE_int32(driver_port, 0, "Server driver port.");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::ThreadPool;
using grpc::testing::Payload;
using grpc::testing::PayloadType;
using grpc::testing::ServerStats;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::StatsRequest;
using grpc::testing::TestService;
using grpc::testing::QpsServer;
using grpc::testing::ServerArgs;
using grpc::testing::ServerStats;
using grpc::testing::ServerStatus;
using grpc::Status;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google { }
namespace gflags { }
using namespace google;
using namespace gflags;

static bool got_sigint = false;

static void sigint_handler(int x) { got_sigint = 1; }

static double time_double(struct timeval* tv) {
  return tv->tv_sec + 1e-6 * tv->tv_usec;
}

static bool SetPayload(PayloadType type, int size, Payload* payload) {
  PayloadType response_type = type;
  // TODO(yangg): Support UNCOMPRESSABLE payload.
  if (type != PayloadType::COMPRESSABLE) {
    return false;
  }
  payload->set_type(response_type);
  std::unique_ptr<char[]> body(new char[size]());
  payload->set_body(body.get(), size);
  return true;
}

namespace {

class TestServiceImpl final : public TestService::Service {
 public:
  Status CollectServerStats(ServerContext* context, const StatsRequest*,
                            ServerStats* response) {
    struct rusage usage;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    getrusage(RUSAGE_SELF, &usage);
    response->set_time_now(time_double(&tv));
    response->set_time_user(time_double(&usage.ru_utime));
    response->set_time_system(time_double(&usage.ru_stime));
    return Status::OK;
  }
  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) {
    if (request->has_response_size() && request->response_size() > 0) {
      if (!SetPayload(request->response_type(), request->response_size(),
                      response->mutable_payload())) {
        return Status(grpc::StatusCode::INTERNAL, "Error creating payload.");
      }
    }
    return Status::OK;
  }
};

}  // namespace

class ServerImpl : public QpsServer::Service {
 public:
  Status RunServer(ServerContext* ctx, ServerReaderWriter<ServerStatus, ServerArgs>* stream) {
    ServerArgs args;
    std::unique_ptr<ServerStats> last_stats;
    if (!stream->Read(&args)) return Status::OK;

    bool done = false;
    while (!done) {
      std::lock_guard<std::mutex> lock(server_mu_);

      char* server_address = NULL;
      gpr_join_host_port(&server_address, "::", FLAGS_port);

      TestServiceImpl service;

      ServerBuilder builder;
      builder.AddPort(server_address);
      builder.RegisterService(&service);

      gpr_free(server_address);

      std::unique_ptr<ThreadPool> pool(new ThreadPool(args.threads()));
      builder.SetThreadPool(pool.get());

      auto server = builder.BuildAndStart();
      gpr_log(GPR_INFO, "Server listening on %s\n", server_address);

      ServerStatus last_status;
      if (last_stats.get()) {
        *last_status.mutable_stats() = *last_stats;
      }
      if (!stream->Write(last_status)) return Status(grpc::UNKNOWN);

      grpc_profiler_start("qps_server.prof");

      done = stream->Read(&args);

      grpc_profiler_stop();
    }

    ServerStatus last_status;
    if (last_stats.get()) {
      *last_status.mutable_stats() = *last_stats;
    }
    stream->Write(last_status);
    return Status::OK;
  }

 private:
  std::mutex server_mu_;
};

static void RunServer() {
  char* server_address = NULL;
  gpr_join_host_port(&server_address, "::", FLAGS_driver_port);

  ServerImpl service;

  ServerBuilder builder;
  builder.AddPort(server_address);
  builder.RegisterService(&service);

  gpr_free(server_address);

  auto server = builder.BuildAndStart();

  while (!got_sigint) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

int main(int argc, char** argv) {
  grpc_init();
  ParseCommandLineFlags(&argc, &argv, true);

  signal(SIGINT, sigint_handler);

  GPR_ASSERT(FLAGS_port != 0);
  RunServer();

  grpc_shutdown();
  return 0;
}
