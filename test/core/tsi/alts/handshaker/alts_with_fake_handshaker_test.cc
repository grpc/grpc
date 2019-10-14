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
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
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

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice_string_helpers.h"

#include "test/core/tsi/alts/fake_handshaker/fake_handshaker_server.h"
#include "test/core/util/memory_counters.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/end2end/cq_verifier.h"

namespace {

void* tag(int i) { return (void*)static_cast<intptr_t>(i); }

void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(
        cq, grpc_timeout_milliseconds_to_deadline(5000), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

class FakeHandshakeServer {
 public:
  FakeHandshakeServer() {
    int port = grpc_pick_unused_port_or_die();
    grpc_core::JoinHostPort(&address_, "localhost", port);
    service_ = grpc::gcp::CreateFakeHandshakerService();
    grpc::ServerBuilder builder;
    // builder.SetResourceQuota(grpc::ResourceQuota("FakeHandshakerService").Resize(2
    // * 1024 * 1024 * 1024/* 2 MB */));
    builder.AddListeningPort(address_.get(), grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Fake handshaker server listening on %s", address_.get());
  }

  ~FakeHandshakeServer() {
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  }

  const char* Address() { return address_.get(); }

 private:
  grpc_core::UniquePtr<char> address_;
  std::unique_ptr<grpc::Service> service_;
  std::unique_ptr<grpc::Server> server_;
};

class TestServer {
 public:
  TestServer(const char* fake_handshake_server_address) {
    grpc_alts_credentials_options* alts_options =
        grpc_alts_credentials_server_options_create();
    grpc_server_credentials* server_creds =
        grpc_alts_server_credentials_create_customized(
            alts_options, fake_handshake_server_address,
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

  const char* Address() { return server_addr_.get(); }

  static void PollUntilShutdown(void* arg) {
    TestServer* self = static_cast<TestServer*>(arg);
    grpc_event ev;
    // Expect to shut down within 30 seconds
    gpr_timespec deadline = grpc_timeout_seconds_to_deadline(30);
    ev = grpc_completion_queue_next(self->server_cq_, deadline, nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == self);
  }

 private:
  grpc_server* server_;
  grpc_completion_queue* server_cq_;
  grpc_core::UniquePtr<grpc_core::Thread> server_thd_;
  grpc_core::UniquePtr<char> server_addr_;
};

struct connect_args {
  const char* server_address;
  const char* fake_handshaker_server_addr;
  const void* debug_id;
  int per_connect_deadline_seconds;
  int loops;
};

void connect_loop(void* arg) {
  connect_args* args = static_cast<connect_args*>(arg);
  void* debug_id = &args;
  for (size_t i = 0; i < args->loops; i++) {
    gpr_log(GPR_DEBUG, "debug_id:%p connect_loop begin loop %ld", debug_id, i);
    grpc_alts_credentials_options* alts_options =
        grpc_alts_credentials_client_options_create();
    grpc_channel_credentials* channel_creds =
        grpc_alts_credentials_create_customized(
            alts_options, args->fake_handshaker_server_addr,
            true /* enable_untrusted_alts */);
    grpc_alts_credentials_options_destroy(alts_options);
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel* channel = grpc_secure_channel_create(
        channel_creds, args->server_address, nullptr, nullptr);
    grpc_channel_credentials_release(channel_creds);
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

void test_basic_client_server_handshake() {
  gpr_log(GPR_DEBUG, "Running test: test_basic_client_server_handshake");
  FakeHandshakeServer fake_handshake_server;
  // Test
  {
    TestServer test_server(fake_handshake_server.Address());
    connect_args args;
    args.fake_handshaker_server_addr = fake_handshake_server.Address();
    args.server_address = test_server.Address();
    args.per_connect_deadline_seconds = 5;
    args.loops = 10;
    connect_loop(&args);
  }
}

/* This test is interesting largely because of the fake handshake server's
 * low resource quota. We make sure that all handshakes succeed, without
 * overloading the fake handshake server. */
void test_concurrent_client_server_handshakes() {
  gpr_log(GPR_DEBUG, "Running test: test_concurrent_client_server_handshakes");
  FakeHandshakeServer fake_handshake_server;
  // Test
  {
    TestServer test_server(fake_handshake_server.Address());
    gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(10);
    int num_concurrent_connects = 100;
    std::vector<grpc_core::UniquePtr<grpc_core::Thread>> thds;
    thds.reserve(num_concurrent_connects);
    connect_args c_args;
    c_args.fake_handshaker_server_addr = fake_handshake_server.Address();
    c_args.server_address = test_server.Address();
    c_args.per_connect_deadline_seconds = 10;
    c_args.loops = 1;
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
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), test_deadline) > 0) {
      gpr_log(GPR_DEBUG, "Test took longer than expected.");
      abort();
    }
  }
}

struct fake_tcp_server_args {
  int port;
  gpr_event stop_ev;
};

void run_fake_tcp_server_that_closes_connections_upon_receiving_bytes(
    void* arg) {
  fake_tcp_server_args* args = static_cast<fake_tcp_server_args*>(arg);
  int s = socket(AF_INET6, SOCK_STREAM, 0);
  GPR_ASSERT(s != -1);
  if (s == -1) {
    gpr_log(GPR_ERROR, "Failed to create socket: %d", errno);
    abort();
  }
  int val = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) != 0) {
    gpr_log(GPR_ERROR,
            "Failed to set SO_REUSEADDR on socket bound to [::1]:%d : %d",
            args->port, errno);
    abort();
  }
  if (fcntl(s, F_SETFL, O_NONBLOCK) != 0) {
    gpr_log(GPR_ERROR, "Failed to set O_NONBLOCK on socket: %d", errno);
    abort();
  }
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(args->port);
  ((char*)&addr.sin6_addr)[15] = 1;
  if (bind(s, (const sockaddr*)&addr, sizeof(addr)) != 0) {
    gpr_log(GPR_ERROR, "Failed to bind socket to [::1]:%d : %d", args->port,
            errno);
    abort();
  }
  if (listen(s, 100)) {
    gpr_log(GPR_ERROR, "Failed to listen on socket bound to [::1]:%d : %d",
            args->port, errno);
    abort();
  }
  std::set<int> peers;
  while (!gpr_event_get(&args->stop_ev)) {
    int p = accept(s, nullptr, nullptr);
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
    for (auto it = peers.begin(); it != peers.end(); it++) {
      int p = *it;
      char buf[100];
      int bytes_received_size = recv(p, buf, 100, 0);
      if (bytes_received_size < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        gpr_log(GPR_ERROR, "Failed to receive from peer socket: %d. errno: %d",
                p, errno);
        abort();
      }
      if (bytes_received_size >= 0) {
        gpr_log(GPR_DEBUG,
                "Fake TCP server received %d bytes from peer socket: %d. Now "
                "close the "
                "connection.",
                bytes_received_size, p);
        close(p);
        peers.erase(p);
      }
    }
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                 gpr_time_from_millis(10, GPR_TIMESPAN)));
  }
  for (auto it = peers.begin(); it != peers.end(); it++) {
    close(*it);
  }
  close(s);
}

void expect_connect_fails_loop(void* arg) {
  connect_args* args = static_cast<connect_args*>(arg);
  void* debug_id = &args;
  for (size_t i = 0; i < args->loops; i++) {
    gpr_log(GPR_DEBUG, "debug_id:%p expect_connect_fails_loop begin loop %ld",
            debug_id, i);
    grpc_alts_credentials_options* alts_options =
        grpc_alts_credentials_client_options_create();
    grpc_channel_credentials* channel_creds =
        grpc_alts_credentials_create_customized(
            alts_options, args->fake_handshaker_server_addr,
            true /* enable_untrusted_alts */);
    grpc_alts_credentials_options_destroy(alts_options);
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel* channel = grpc_secure_channel_create(
        channel_creds, args->server_address, nullptr, nullptr);
    grpc_channel_credentials_release(channel_creds);
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

/* This test is intended to make sure that we quickly cancel ALTS RPC's
 * when the security handshaker gets a read endpoint from the remote peer. The
 * goal is that RPC's will sharply slow down due to exceeding the number
 * of handshakes that can be outstanding at once, forcing new handshakes to be
 * queued up for longer than they should be if that isn't done. */
void test_handshake_fails_fast_when_peer_endpoint_closes_connection_after_accepting() {
  gpr_log(GPR_DEBUG,
          "Running test: "
          "test_handshake_fails_fast_when_peer_endpoint_closes_connection_"
          "after_accepting");
  FakeHandshakeServer fake_handshake_server;
  {
    fake_tcp_server_args args;
    memset(&args, 0, sizeof(args));
    args.port = grpc_pick_unused_port_or_die();
    gpr_event_init(&args.stop_ev);
    grpc_core::UniquePtr<grpc_core::Thread> fake_tcp_server_thd =
        grpc_core::MakeUnique<grpc_core::Thread>(
            "fake tcp server that closes connections upon receiving bytes",
            run_fake_tcp_server_that_closes_connections_upon_receiving_bytes,
            &args);
    fake_tcp_server_thd->Start();
    grpc_core::UniquePtr<char> fake_tcp_server_addr;
    grpc_core::JoinHostPort(&fake_tcp_server_addr, "[::1]", args.port);
    {
      gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(10);
      std::vector<grpc_core::UniquePtr<grpc_core::Thread>> connect_thds;
      int num_concurrent_connects = 100;
      connect_thds.reserve(num_concurrent_connects);
      connect_args c_args;
      c_args.server_address = fake_tcp_server_addr.get();
      c_args.fake_handshaker_server_addr = fake_handshake_server.Address();
      c_args.loops = 5;
      c_args.per_connect_deadline_seconds = 10;
      gpr_log(GPR_DEBUG, "start performing concurrent connect expect failures");
      for (size_t i = 0; i < num_concurrent_connects; i++) {
        auto new_thd = grpc_core::MakeUnique<grpc_core::Thread>(
            "connect fails fast", expect_connect_fails_loop, &c_args);
        new_thd->Start();
        connect_thds.push_back(std::move(new_thd));
      }
      for (size_t i = 0; i < num_concurrent_connects; i++) {
        connect_thds[i]->Join();
      }
      gpr_event_set(&args.stop_ev, (void*)1);
      gpr_log(GPR_DEBUG, "done performing concurrent connect expect failures");
      if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), test_deadline) > 0) {
        gpr_log(GPR_ERROR,
                "Exceeded test deadline. ALTS handshakes might not be failing "
                "fast when the peer endpoint closes the connection abruptly");
        abort();
      }
    }
    fake_tcp_server_thd->Join();
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
