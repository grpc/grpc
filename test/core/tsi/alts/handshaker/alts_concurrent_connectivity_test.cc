/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <set>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <grpcpp/impl/codegen/service_type.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/alts/alts_security_connector.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include "test/core/tsi/alts/fake_handshaker/fake_handshaker_server.h"
#include "test/core/util/memory_counters.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/end2end/cq_verifier.h"

namespace {

void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(
        cq, grpc_timeout_milliseconds_to_deadline(5000), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

grpc_channel* create_secure_channel_for_test(
    const char* server_addr, const char* fake_handshake_server_addr,
    const void* debug_id) {
  grpc_alts_credentials_options* alts_options =
      grpc_alts_credentials_client_options_create();
  grpc_channel_credentials* channel_creds =
      grpc_alts_credentials_create_customized(alts_options,
                                              fake_handshake_server_addr,
                                              true /* enable_untrusted_alts */);
  grpc_alts_credentials_options_destroy(alts_options);
  // The main goal of these tests are to stress concurrent ALTS handshakes,
  // so we prevent subchnannel sharing.
  char* log_trace_id;
  GPR_ASSERT(gpr_asprintf(&log_trace_id, "%p", debug_id));
  grpc_arg extra_arg;
  extra_arg.type = GRPC_ARG_STRING;
  extra_arg.key = const_cast<char*>("ensure_subchannel_sharing_disabled");
  extra_arg.value.string = log_trace_id;
  grpc_channel_args* channel_args =
      grpc_channel_args_copy_and_add(nullptr, &extra_arg, 1);
  gpr_free(log_trace_id);
  grpc_channel* channel = grpc_secure_channel_create(channel_creds, server_addr,
                                                     channel_args, nullptr);
  grpc_channel_args_destroy(channel_args);
  grpc_channel_credentials_release(channel_creds);
  return channel;
}

class FakeHandshakeServer {
 public:
  FakeHandshakeServer() {
    int port = grpc_pick_unused_port_or_die();
    grpc_core::JoinHostPort(&address_, "localhost", port);
    service_ = grpc::gcp::CreateFakeHandshakerService();
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address_.get(), grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Fake handshaker server listening on %s", address_.get());
  }

  ~FakeHandshakeServer() {
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  }

  grpc_core::UniquePtr<char> Address() {
    return grpc_core::UniquePtr<char>(gpr_strdup(address_.get()));
  }

 private:
  grpc_core::UniquePtr<char> address_;
  std::unique_ptr<grpc::Service> service_;
  std::unique_ptr<grpc::Server> server_;
};

class TestServer {
 public:
  TestServer(grpc_core::UniquePtr<char> fake_handshake_server_address) {
    grpc_alts_credentials_options* alts_options =
        grpc_alts_credentials_server_options_create();
    grpc_server_credentials* server_creds =
        grpc_alts_server_credentials_create_customized(
            alts_options, fake_handshake_server_address.get(),
            true /* enable_untrusted_alts */);
    grpc_alts_credentials_options_destroy(alts_options);
    server_ = grpc_server_create(nullptr, nullptr);
    server_cq_ = grpc_completion_queue_create_for_next(nullptr);
    grpc_server_register_completion_queue(server_, server_cq_, nullptr);
    int port = grpc_pick_unused_port_or_die();
    GPR_ASSERT(grpc_core::JoinHostPort(&server_addr_, "localhost", port));
    GPR_ASSERT(grpc_server_add_secure_http2_port(server_, server_addr_.get(),
                                                 server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server_);
    gpr_log(GPR_DEBUG, "Start TestServer %p. listen on %s", this,
            server_addr_.get());
    server_thd_ = grpc_core::MakeUnique<grpc_core::Thread>(
        "server thread", PollUntilShutdown, this);
    server_thd_->Start();
  }

  ~TestServer() {
    gpr_log(GPR_DEBUG, "Begin dtor of TestServer %p", this);
    grpc_server_shutdown_and_notify(server_, server_cq_, this);
    server_thd_->Join();
    grpc_server_destroy(server_);
    grpc_completion_queue_shutdown(server_cq_);
    drain_cq(server_cq_);
    grpc_completion_queue_destroy(server_cq_);
  }

  grpc_core::UniquePtr<char> Address() {
    return grpc_core::UniquePtr<char>(gpr_strdup(server_addr_.get()));
  }

  static void PollUntilShutdown(void* arg) {
    TestServer* self = static_cast<TestServer*>(arg);
    grpc_event ev = grpc_completion_queue_next(
        self->server_cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == self);
    gpr_log(GPR_DEBUG, "TestServer %p stop polling", self);
  }

 private:
  grpc_server* server_;
  grpc_completion_queue* server_cq_;
  grpc_core::UniquePtr<grpc_core::Thread> server_thd_;
  grpc_core::UniquePtr<char> server_addr_;
};

struct connect_args {
  grpc_core::UniquePtr<char> server_address;
  grpc_core::UniquePtr<char> fake_handshaker_server_addr;
  const void* debug_id;
  int per_connect_deadline_seconds;
  int loops;
};

void connect_loop(void* arg) {
  connect_args* args = static_cast<connect_args*>(arg);
  void* debug_id = &args;
  for (size_t i = 0; i < args->loops; i++) {
    gpr_log(GPR_DEBUG, "debug_id:%p connect_loop begin loop %ld", debug_id, i);
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel* channel = create_secure_channel_for_test(
        args->server_address.get(), args->fake_handshaker_server_addr.get(),
        debug_id);
    // Connect, forcing an ALTS handshake
    gpr_timespec connect_deadline =
        grpc_timeout_seconds_to_deadline(args->per_connect_deadline_seconds);
    grpc_connectivity_state state =
        grpc_channel_check_connectivity_state(channel, 1);
    GPR_ASSERT(state == GRPC_CHANNEL_IDLE);
    while (state != GRPC_CHANNEL_READY) {
      grpc_channel_watch_connectivity_state(
          channel, state, gpr_inf_future(GPR_CLOCK_REALTIME), cq, nullptr);
      grpc_event ev = grpc_completion_queue_next(cq, connect_deadline, nullptr);
      if (ev.type != GRPC_OP_COMPLETE) {
        gpr_log(GPR_ERROR, "connect_loop debug_id:%p got ev.type:%d i:%ld",
                debug_id, ev.type, i);
        abort();
      }
      GPR_ASSERT(ev.success);
      state = grpc_channel_check_connectivity_state(channel, 1);
    }
    grpc_channel_destroy(channel);
    grpc_completion_queue_shutdown(cq);
    drain_cq(cq);
    grpc_completion_queue_destroy(cq);
    gpr_log(GPR_DEBUG, "debug_id:%p connect_loop finished loop %ld", debug_id,
            i);
  }
}

// Perform a few ALTS handshakes sequentially (using the fake, in-process ALTS
// handshake server).
void test_basic_client_server_handshake() {
  gpr_log(GPR_DEBUG, "Running test: test_basic_client_server_handshake");
  FakeHandshakeServer fake_handshake_server;
  TestServer test_server(fake_handshake_server.Address());
  {
    connect_args args;
    args.fake_handshaker_server_addr = fake_handshake_server.Address();
    args.server_address = test_server.Address();
    args.per_connect_deadline_seconds = 5;
    args.loops = 10;
    connect_loop(&args);
  }
}

/* Run a bunch of concurrent ALTS handshakes on concurrent channels
 * (using the fake, in-process handshake server). */
void test_concurrent_client_server_handshakes() {
  gpr_log(GPR_DEBUG, "Running test: test_concurrent_client_server_handshakes");
  FakeHandshakeServer fake_handshake_server;
  // Test
  {
    TestServer test_server(fake_handshake_server.Address());
    gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(20);
    int num_concurrent_connects = 50;
    std::vector<grpc_core::UniquePtr<grpc_core::Thread>> thds;
    thds.reserve(num_concurrent_connects);
    connect_args c_args;
    c_args.fake_handshaker_server_addr = fake_handshake_server.Address();
    c_args.server_address = test_server.Address();
    c_args.per_connect_deadline_seconds = 15;
    c_args.loops = 5;
    gpr_log(GPR_DEBUG,
            "start performing concurrent expected-to-succeed connects");
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      auto new_thd = grpc_core::MakeUnique<grpc_core::Thread>(
          "test_concurrent_client_server_handshakes thd", connect_loop,
          &c_args);
      new_thd->Start();
      thds.push_back(std::move(new_thd));
    }
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      thds[i]->Join();
    }
    gpr_log(GPR_DEBUG,
            "done performing concurrent expected-to-succeed connects");
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), test_deadline) > 0) {
      gpr_log(GPR_DEBUG, "Test took longer than expected.");
      abort();
    }
  }
}

class FakeTcpServer {
 public:
  enum ProcessReadResult {
    CONTINUE_READING,
    CLOSE_SOCKET,
  };

  FakeTcpServer(
      const std::function<ProcessReadResult(int, int, int)>& process_read_cb)
      : process_read_cb_(process_read_cb) {
    port_ = grpc_pick_unused_port_or_die();
    accept_socket_ = socket(AF_INET6, SOCK_STREAM, 0);
    GPR_ASSERT(accept_socket_ != -1);
    if (accept_socket_ == -1) {
      gpr_log(GPR_ERROR, "Failed to create socket: %d", errno);
      abort();
    }
    int val = 1;
    if (setsockopt(accept_socket_, SOL_SOCKET, SO_REUSEADDR, &val,
                   sizeof(val)) != 0) {
      gpr_log(GPR_ERROR,
              "Failed to set SO_REUSEADDR on socket bound to [::1]:%d : %d",
              port_, errno);
      abort();
    }
    if (fcntl(accept_socket_, F_SETFL, O_NONBLOCK) != 0) {
      gpr_log(GPR_ERROR, "Failed to set O_NONBLOCK on socket: %d", errno);
      abort();
    }
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port_);
    ((char*)&addr.sin6_addr)[15] = 1;
    if (bind(accept_socket_, (const sockaddr*)&addr, sizeof(addr)) != 0) {
      gpr_log(GPR_ERROR, "Failed to bind socket to [::1]:%d : %d", port_,
              errno);
      abort();
    }
    if (listen(accept_socket_, 100)) {
      gpr_log(GPR_ERROR, "Failed to listen on socket bound to [::1]:%d : %d",
              port_, errno);
      abort();
    }
    gpr_event_init(&stop_ev_);
    run_server_loop_thd_ = grpc_core::MakeUnique<grpc_core::Thread>(
        "fake tcp server that closes connections upon receiving bytes",
        RunServerLoop, this);
    run_server_loop_thd_->Start();
  }

  ~FakeTcpServer() {
    gpr_log(GPR_DEBUG,
            "FakeTcpServer stop and "
            "join server thread");
    gpr_event_set(&stop_ev_, (void*)1);
    run_server_loop_thd_->Join();
    gpr_log(GPR_DEBUG,
            "FakeTcpServer join server "
            "thread complete");
  }

  grpc_core::UniquePtr<char> Address() {
    char* addr;
    GPR_ASSERT(gpr_asprintf(&addr, "[::]:%d", port_));
    return grpc_core::UniquePtr<char>(addr);
  }

  static ProcessReadResult CloseSocketUponReceivingBytesFromPeer(
      int bytes_received_size, int read_error, int s) {
    if (bytes_received_size < 0 && read_error != EAGAIN &&
        read_error != EWOULDBLOCK) {
      gpr_log(GPR_ERROR, "Failed to receive from peer socket: %d. errno: %d", s,
              errno);
      abort();
    }
    if (bytes_received_size >= 0) {
      gpr_log(GPR_DEBUG,
              "Fake TCP server received %d bytes from peer socket: %d. Close "
              "the "
              "connection.",
              bytes_received_size, s);
      return CLOSE_SOCKET;
    }
    return CONTINUE_READING;
  }

  static ProcessReadResult CloseSocketUponCloseFromPeer(int bytes_received_size,
                                                        int read_error, int s) {
    if (bytes_received_size < 0 && read_error != EAGAIN &&
        read_error != EWOULDBLOCK) {
      gpr_log(GPR_ERROR, "Failed to receive from peer socket: %d. errno: %d", s,
              errno);
      abort();
    }
    if (bytes_received_size == 0) {
      // The peer has shut down the connection.
      gpr_log(GPR_DEBUG,
              "Fake TCP server received 0 bytes from peer socket: %d. Close "
              "the "
              "connection.",
              s);
      return CLOSE_SOCKET;
    }
    return CONTINUE_READING;
  }

  // Run a loop that periodically, every 10 ms:
  //   1) Checks if there are any new TCP connections to accept.
  //   2) Checks if any data has arrived yet on established connections,
  //      and closes them if so.
  static void RunServerLoop(void* arg) {
    FakeTcpServer* self = static_cast<FakeTcpServer*>(arg);
    std::set<int> peers;
    while (!gpr_event_get(&self->stop_ev_)) {
      int p = accept(self->accept_socket_, nullptr, nullptr);
      if (p == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        gpr_log(GPR_ERROR, "Failed to accept connection: %d", errno);
        abort();
      }
      if (p != -1) {
        gpr_log(GPR_DEBUG, "accepted peer socket: %d", p);
        if (fcntl(p, F_SETFL, O_NONBLOCK) != 0) {
          gpr_log(GPR_ERROR, "Failed to set O_NONBLOCK on peer socket: %d", p,
                  errno);
          abort();
        }
        peers.insert(p);
      }
      auto it = peers.begin();
      while (it != peers.end()) {
        int p = *it;
        char buf[100];
        int bytes_received_size = recv(p, buf, 100, 0);
        ProcessReadResult r =
            self->process_read_cb_(bytes_received_size, errno, p);
        if (r == CLOSE_SOCKET) {
          close(p);
          it = peers.erase(it);
        } else {
          GPR_ASSERT(r == CONTINUE_READING);
          it++;
        }
      }
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_millis(10, GPR_TIMESPAN)));
    }
    for (auto it = peers.begin(); it != peers.end(); it++) {
      close(*it);
    }
    close(self->accept_socket_);
  }

 private:
  int accept_socket_;
  int port_;
  gpr_event stop_ev_;
  grpc_core::UniquePtr<grpc_core::Thread> run_server_loop_thd_;
  std::function<ProcessReadResult(int, int, int)> process_read_cb_;
};

void expect_connect_fails_loop(void* arg) {
  connect_args* args = static_cast<connect_args*>(arg);
  void* debug_id = &args;
  for (size_t i = 0; i < args->loops; i++) {
    gpr_log(GPR_DEBUG, "debug_id:%p expect_connect_fails_loop begin loop %ld",
            debug_id, i);
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel* channel = create_secure_channel_for_test(
        args->server_address.get(), args->fake_handshaker_server_addr.get(),
        debug_id);
    // Connect, forcing an ALTS handshake attempt
    gpr_timespec connect_failure_deadline =
        grpc_timeout_seconds_to_deadline(args->per_connect_deadline_seconds);
    grpc_connectivity_state state =
        grpc_channel_check_connectivity_state(channel, 1);
    GPR_ASSERT(state == GRPC_CHANNEL_IDLE);
    while (state != GRPC_CHANNEL_TRANSIENT_FAILURE) {
      GPR_ASSERT(state != GRPC_CHANNEL_READY);  // sanity check
      grpc_channel_watch_connectivity_state(
          channel, state, gpr_inf_future(GPR_CLOCK_REALTIME), cq, nullptr);
      grpc_event ev =
          grpc_completion_queue_next(cq, connect_failure_deadline, nullptr);
      if (ev.type != GRPC_OP_COMPLETE) {
        gpr_log(GPR_ERROR,
                "expect_connect_fails_loop debug_id:%p got ev.type:%d i:%ld",
                debug_id, ev.type, i);
        abort();
      }
      state = grpc_channel_check_connectivity_state(channel, 1);
    }
    grpc_channel_destroy(channel);
    grpc_completion_queue_shutdown(cq);
    drain_cq(cq);
    grpc_completion_queue_destroy(cq);
    gpr_log(GPR_DEBUG,
            "debug_id:%p expect_connect_fails_loop finished loop %ld", debug_id,
            i);
  }
}

/* This test is intended to make sure that ALTS handshakes we correctly
 * fail fast when the security handshaker gets an error while reading
 * from the remote peer, after having earlier sent the first bytes of the
 * ALTS handshake to the peer, i.e. after getting into the middle of a
 * handshake. */
void test_handshake_fails_fast_when_peer_endpoint_closes_connection_after_accepting() {
  gpr_log(GPR_DEBUG,
          "Running test: "
          "test_handshake_fails_fast_when_peer_endpoint_closes_connection_"
          "after_accepting");
  FakeHandshakeServer fake_handshake_server;
  FakeTcpServer fake_tcp_server(
      FakeTcpServer::CloseSocketUponReceivingBytesFromPeer);
  {
    gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(20);
    std::vector<grpc_core::UniquePtr<grpc_core::Thread>> connect_thds;
    int num_concurrent_connects = 100;
    connect_thds.reserve(num_concurrent_connects);
    connect_args c_args;
    c_args.server_address = fake_tcp_server.Address();
    c_args.fake_handshaker_server_addr = fake_handshake_server.Address();
    c_args.per_connect_deadline_seconds = 10;
    c_args.loops = 5;
    gpr_log(GPR_DEBUG, "start performing concurrent expected-to-fail connects");
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      auto new_thd = grpc_core::MakeUnique<grpc_core::Thread>(
          "connect fails fast", expect_connect_fails_loop, &c_args);
      new_thd->Start();
      connect_thds.push_back(std::move(new_thd));
    }
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      connect_thds[i]->Join();
    }
    gpr_log(GPR_DEBUG, "done performing concurrent expected-to-fail connects");
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), test_deadline) > 0) {
      gpr_log(GPR_ERROR,
              "Exceeded test deadline. ALTS handshakes might not be failing "
              "fast when the peer endpoint closes the connection abruptly");
      abort();
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  grpc_init();
  {
    test_basic_client_server_handshake();
    test_concurrent_client_server_handshakes();
    test_handshake_fails_fast_when_peer_endpoint_closes_connection_after_accepting();
  }
  grpc_shutdown();
  return 0;
}
