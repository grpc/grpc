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

// Test description at doc/connection-backoff-interop-test-description.md

#include <signal.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <sstream>

#include <gflags/gflags.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/reconnect_server.h"
#include "test/cpp/util/test_config.h"

DEFINE_int32(control_port, 0, "Server port for controlling the server.");
DEFINE_int32(retry_port, 0,
             "Server port for raw tcp connections. All incoming "
             "connections will be closed immediately.");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerCredentials;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::SslServerCredentialsOptions;
using grpc::Status;
using grpc::testing::Empty;
using grpc::testing::ReconnectInfo;
using grpc::testing::ReconnectParams;
using grpc::testing::ReconnectService;

static bool got_sigint = false;

class ReconnectServiceImpl : public ReconnectService::Service {
 public:
  explicit ReconnectServiceImpl(int retry_port)
      : retry_port_(retry_port),
        serving_(false),
        server_started_(false),
        shutdown_(false) {
    reconnect_server_init(&tcp_server_);
  }

  ~ReconnectServiceImpl() {
    if (server_started_) {
      reconnect_server_destroy(&tcp_server_);
    }
  }

  void Poll(int seconds) { reconnect_server_poll(&tcp_server_, seconds); }

  Status Start(ServerContext* context, const ReconnectParams* request,
               Empty* response) {
    bool start_server = true;
    std::unique_lock<std::mutex> lock(mu_);
    while (serving_ && !shutdown_) {
      cv_.wait(lock);
    }
    if (shutdown_) {
      return Status(grpc::StatusCode::UNAVAILABLE, "shutting down");
    }
    serving_ = true;
    if (server_started_) {
      start_server = false;
    } else {
      tcp_server_.max_reconnect_backoff_ms =
          request->max_reconnect_backoff_ms();
      server_started_ = true;
    }
    lock.unlock();

    if (start_server) {
      reconnect_server_start(&tcp_server_, retry_port_);
    } else {
      reconnect_server_clear_timestamps(&tcp_server_);
    }
    return Status::OK;
  }

  Status Stop(ServerContext* context, const Empty* request,
              ReconnectInfo* response) {
    // extract timestamps and set response
    Verify(response);
    reconnect_server_clear_timestamps(&tcp_server_);
    std::lock_guard<std::mutex> lock(mu_);
    serving_ = false;
    cv_.notify_one();
    return Status::OK;
  }

  void Verify(ReconnectInfo* response) {
    double expected_backoff = 1000.0;
    const double kTransmissionDelay = 100.0;
    const double kBackoffMultiplier = 1.6;
    const double kJitterFactor = 0.2;
    const int kMaxBackoffMs = tcp_server_.max_reconnect_backoff_ms
                                  ? tcp_server_.max_reconnect_backoff_ms
                                  : 120 * 1000;
    bool passed = true;
    for (timestamp_list* cur = tcp_server_.head; cur && cur->next;
         cur = cur->next) {
      double backoff = gpr_time_to_millis(
          gpr_time_sub(cur->next->timestamp, cur->timestamp));
      double min_backoff = expected_backoff * (1 - kJitterFactor);
      double max_backoff = expected_backoff * (1 + kJitterFactor);
      if (backoff < min_backoff - kTransmissionDelay ||
          backoff > max_backoff + kTransmissionDelay) {
        passed = false;
      }
      response->add_backoff_ms(static_cast<int32_t>(backoff));
      expected_backoff *= kBackoffMultiplier;
      expected_backoff =
          expected_backoff > kMaxBackoffMs ? kMaxBackoffMs : expected_backoff;
    }
    response->set_passed(passed);
  }

  void Shutdown() {
    std::lock_guard<std::mutex> lock(mu_);
    shutdown_ = true;
    cv_.notify_all();
  }

 private:
  int retry_port_;
  reconnect_server tcp_server_;
  bool serving_;
  bool server_started_;
  bool shutdown_;
  std::mutex mu_;
  std::condition_variable cv_;
};

void RunServer() {
  std::ostringstream server_address;
  server_address << "0.0.0.0:" << FLAGS_control_port;
  ReconnectServiceImpl service(FLAGS_retry_port);

  ServerBuilder builder;
  builder.RegisterService(&service);
  builder.AddListeningPort(server_address.str(),
                           grpc::InsecureServerCredentials());
  std::unique_ptr<Server> server(builder.BuildAndStart());
  gpr_log(GPR_INFO, "Server listening on %s", server_address.str().c_str());
  while (!got_sigint) {
    service.Poll(5);
  }
  service.Shutdown();
}

static void sigint_handler(int x) { got_sigint = true; }

int main(int argc, char** argv) {
  grpc::testing::InitTest(&argc, &argv, true);
  signal(SIGINT, sigint_handler);

  GPR_ASSERT(FLAGS_control_port != 0);
  GPR_ASSERT(FLAGS_retry_port != 0);
  RunServer();

  return 0;
}
