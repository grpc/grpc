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
#include <gmock/gmock.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <functional>
#include <set>
#include <thread>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

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

const int kFakeHandshakeServerMaxConcurrentStreams = 40;

void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(
        cq, grpc_timeout_milliseconds_to_deadline(5000), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

grpc_channel* create_secure_channel_for_test(
    const char* server_addr, const char* fake_handshake_server_addr,
    int reconnect_backoff_ms) {
  grpc_alts_credentials_options* alts_options =
      grpc_alts_credentials_client_options_create();
  grpc_channel_credentials* channel_creds =
      grpc_alts_credentials_create_customized(alts_options,
                                              fake_handshake_server_addr,
                                              true /* enable_untrusted_alts */);
  grpc_alts_credentials_options_destroy(alts_options);
  // The main goal of these tests are to stress concurrent ALTS handshakes,
  // so we prevent subchnannel sharing.
  std::vector<grpc_arg> new_args;
  new_args.push_back(grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL), true));
  if (reconnect_backoff_ms != 0) {
    new_args.push_back(grpc_channel_arg_integer_create(
        const_cast<char*>("grpc.testing.fixed_reconnect_backoff_ms"),
        reconnect_backoff_ms));
  }
  grpc_channel_args* channel_args =
      grpc_channel_args_copy_and_add(nullptr, new_args.data(), new_args.size());
  grpc_channel* channel = grpc_secure_channel_create(channel_creds, server_addr,
                                                     channel_args, nullptr);
  grpc_channel_args_destroy(channel_args);
  grpc_channel_credentials_release(channel_creds);
  return channel;
}

class FakeHandshakeServer {
 public:
  explicit FakeHandshakeServer(bool check_num_concurrent_rpcs) {
    int port = grpc_pick_unused_port_or_die();
    address_ = grpc_core::JoinHostPort("localhost", port);
    if (check_num_concurrent_rpcs) {
      service_ = grpc::gcp::
          CreateFakeHandshakerService(kFakeHandshakeServerMaxConcurrentStreams /* expected max concurrent rpcs */);
    } else {
      service_ = grpc::gcp::CreateFakeHandshakerService(
          0 /* expected max concurrent rpcs unset */);
    }
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address_.c_str(),
                             grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    // TODO(apolcyn): when removing the global concurrent handshake limiting
    // queue, set MAX_CONCURRENT_STREAMS on this server.
    server_ = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Fake handshaker server listening on %s",
            address_.c_str());
  }

  ~FakeHandshakeServer() {
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
  }

  const char* address() { return address_.c_str(); }

 private:
  std::string address_;
  std::unique_ptr<grpc::Service> service_;
  std::unique_ptr<grpc::Server> server_;
};

class TestServer {
 public:
  explicit TestServer()
      : fake_handshake_server_(true /* check num concurrent rpcs */) {
    grpc_alts_credentials_options* alts_options =
        grpc_alts_credentials_server_options_create();
    grpc_server_credentials* server_creds =
        grpc_alts_server_credentials_create_customized(
            alts_options, fake_handshake_server_.address(),
            true /* enable_untrusted_alts */);
    grpc_alts_credentials_options_destroy(alts_options);
    server_ = grpc_server_create(nullptr, nullptr);
    server_cq_ = grpc_completion_queue_create_for_next(nullptr);
    grpc_server_register_completion_queue(server_, server_cq_, nullptr);
    int port = grpc_pick_unused_port_or_die();
    server_addr_ = grpc_core::JoinHostPort("localhost", port);
    GPR_ASSERT(grpc_server_add_secure_http2_port(server_, server_addr_.c_str(),
                                                 server_creds));
    grpc_server_credentials_release(server_creds);
    grpc_server_start(server_);
    gpr_log(GPR_DEBUG, "Start TestServer %p. listen on %s", this,
            server_addr_.c_str());
    server_thd_ = absl::make_unique<std::thread>(PollUntilShutdown, this);
  }

  ~TestServer() {
    gpr_log(GPR_DEBUG, "Begin dtor of TestServer %p", this);
    grpc_server_shutdown_and_notify(server_, server_cq_, this);
    server_thd_->join();
    grpc_server_destroy(server_);
    grpc_completion_queue_shutdown(server_cq_);
    drain_cq(server_cq_);
    grpc_completion_queue_destroy(server_cq_);
  }

  const char* address() { return server_addr_.c_str(); }

  static void PollUntilShutdown(const TestServer* self) {
    grpc_event ev = grpc_completion_queue_next(
        self->server_cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
    GPR_ASSERT(ev.tag == self);
    gpr_log(GPR_DEBUG, "TestServer %p stop polling", self);
  }

 private:
  grpc_server* server_;
  grpc_completion_queue* server_cq_;
  std::unique_ptr<std::thread> server_thd_;
  std::string server_addr_;
  // Give this test server its own ALTS handshake server
  // so that we avoid competing for ALTS handshake server resources (e.g.
  // available HTTP2 streams on a globally shared handshaker subchannel)
  // with clients that are trying to do mutual ALTS handshakes
  // with this server (which could "deadlock" mutual handshakes).
  // TODO(apolcyn): remove this workaround from this test and have
  // clients/servers share a single fake handshake server if
  // the underlying issue needs to be fixed.
  FakeHandshakeServer fake_handshake_server_;
};

class ConnectLoopRunner {
 public:
  explicit ConnectLoopRunner(
      const char* server_address, const char* fake_handshake_server_addr,
      int per_connect_deadline_seconds, size_t loops,
      grpc_connectivity_state expected_connectivity_states,
      int reconnect_backoff_ms)
      : server_address_(grpc_core::UniquePtr<char>(gpr_strdup(server_address))),
        fake_handshake_server_addr_(
            grpc_core::UniquePtr<char>(gpr_strdup(fake_handshake_server_addr))),
        per_connect_deadline_seconds_(per_connect_deadline_seconds),
        loops_(loops),
        expected_connectivity_states_(expected_connectivity_states),
        reconnect_backoff_ms_(reconnect_backoff_ms) {
    thd_ = absl::make_unique<std::thread>(ConnectLoop, this);
  }

  ~ConnectLoopRunner() { thd_->join(); }

  static void ConnectLoop(const ConnectLoopRunner* self) {
    for (size_t i = 0; i < self->loops_; i++) {
      gpr_log(GPR_DEBUG, "runner:%p connect_loop begin loop %ld", self, i);
      grpc_completion_queue* cq =
          grpc_completion_queue_create_for_next(nullptr);
      grpc_channel* channel = create_secure_channel_for_test(
          self->server_address_.get(), self->fake_handshake_server_addr_.get(),
          self->reconnect_backoff_ms_);
      // Connect, forcing an ALTS handshake
      gpr_timespec connect_deadline =
          grpc_timeout_seconds_to_deadline(self->per_connect_deadline_seconds_);
      grpc_connectivity_state state =
          grpc_channel_check_connectivity_state(channel, 1);
      ASSERT_EQ(state, GRPC_CHANNEL_IDLE);
      while (state != self->expected_connectivity_states_) {
        if (self->expected_connectivity_states_ ==
            GRPC_CHANNEL_TRANSIENT_FAILURE) {
          ASSERT_NE(state, GRPC_CHANNEL_READY);  // sanity check
        } else {
          ASSERT_EQ(self->expected_connectivity_states_, GRPC_CHANNEL_READY);
        }
        grpc_channel_watch_connectivity_state(
            channel, state, gpr_inf_future(GPR_CLOCK_REALTIME), cq, nullptr);
        grpc_event ev =
            grpc_completion_queue_next(cq, connect_deadline, nullptr);
        ASSERT_EQ(ev.type, GRPC_OP_COMPLETE)
            << "connect_loop runner:" << std::hex << self
            << " got ev.type:" << ev.type << " i:" << i;
        ASSERT_TRUE(ev.success);
        grpc_connectivity_state prev_state = state;
        state = grpc_channel_check_connectivity_state(channel, 1);
        if (self->expected_connectivity_states_ ==
                GRPC_CHANNEL_TRANSIENT_FAILURE &&
            prev_state == GRPC_CHANNEL_CONNECTING &&
            state == GRPC_CHANNEL_CONNECTING) {
          // Detect a race in state checking: if the watch_connectivity_state
          // completed from prior state "connecting", this could be because the
          // channel momentarily entered state "transient failure", which is
          // what we want. However, if the channel immediately re-enters
          // "connecting" state, then the new state check might still result in
          // "connecting". A continuous repeat of this can cause this loop to
          // never terminate in time. So take this scenario to indicate that the
          // channel momentarily entered transient failure.
          break;
        }
      }
      grpc_channel_destroy(channel);
      grpc_completion_queue_shutdown(cq);
      drain_cq(cq);
      grpc_completion_queue_destroy(cq);
      gpr_log(GPR_DEBUG, "runner:%p connect_loop finished loop %ld", self, i);
    }
  }

 private:
  grpc_core::UniquePtr<char> server_address_;
  grpc_core::UniquePtr<char> fake_handshake_server_addr_;
  int per_connect_deadline_seconds_;
  size_t loops_;
  grpc_connectivity_state expected_connectivity_states_;
  std::unique_ptr<std::thread> thd_;
  int reconnect_backoff_ms_;
};

// Perform a few ALTS handshakes sequentially (using the fake, in-process ALTS
// handshake server).
TEST(AltsConcurrentConnectivityTest, TestBasicClientServerHandshakes) {
  FakeHandshakeServer fake_handshake_server(
      true /* check num concurrent rpcs */);
  TestServer test_server;
  {
    ConnectLoopRunner runner(
        test_server.address(), fake_handshake_server.address(),
        5 /* per connect deadline seconds */, 10 /* loops */,
        GRPC_CHANNEL_READY /* expected connectivity states */,
        0 /* reconnect_backoff_ms unset */);
  }
}

/* Run a bunch of concurrent ALTS handshakes on concurrent channels
 * (using the fake, in-process handshake server). */
TEST(AltsConcurrentConnectivityTest, TestConcurrentClientServerHandshakes) {
  FakeHandshakeServer fake_handshake_server(
      true /* check num concurrent rpcs */);
  // Test
  {
    TestServer test_server;
    gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(20);
    size_t num_concurrent_connects = 50;
    std::vector<std::unique_ptr<ConnectLoopRunner>> connect_loop_runners;
    gpr_log(GPR_DEBUG,
            "start performing concurrent expected-to-succeed connects");
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      connect_loop_runners.push_back(absl::make_unique<ConnectLoopRunner>(
          test_server.address(), fake_handshake_server.address(),
          15 /* per connect deadline seconds */, 5 /* loops */,
          GRPC_CHANNEL_READY /* expected connectivity states */,
          0 /* reconnect_backoff_ms unset */));
    }
    connect_loop_runners.clear();
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

  enum class AcceptMode {
    kWaitForClientToSendFirstBytes,  // useful for emulating ALTS based
                                     // grpc servers
    kEagerlySendSettings,  // useful for emulating insecure grpc servers (e.g.
                           // ALTS handshake servers)
  };

  explicit FakeTcpServer(
      AcceptMode accept_mode,
      const std::function<ProcessReadResult(int, int, int)>& process_read_cb)
      : accept_mode_(accept_mode), process_read_cb_(process_read_cb) {
    port_ = grpc_pick_unused_port_or_die();
    accept_socket_ = socket(AF_INET6, SOCK_STREAM, 0);
    address_ = absl::StrCat("[::]:", port_);
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
    (reinterpret_cast<char*>(&addr.sin6_addr))[15] = 1;
    if (bind(accept_socket_, reinterpret_cast<const sockaddr*>(&addr),
             sizeof(addr)) != 0) {
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
    run_server_loop_thd_ = absl::make_unique<std::thread>(RunServerLoop, this);
  }

  ~FakeTcpServer() {
    gpr_log(GPR_DEBUG,
            "FakeTcpServer stop and "
            "join server thread");
    gpr_event_set(&stop_ev_, reinterpret_cast<void*>(1));
    run_server_loop_thd_->join();
    gpr_log(GPR_DEBUG,
            "FakeTcpServer join server "
            "thread complete");
  }

  const char* address() { return address_.c_str(); }

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

  class FakeTcpServerPeer {
   public:
    explicit FakeTcpServerPeer(int fd) : fd_(fd) {}

    ~FakeTcpServerPeer() { close(fd_); }

    void MaybeContinueSendingSettings() {
      // https://tools.ietf.org/html/rfc7540#section-4.1
      const std::vector<uint8_t> kEmptyHttp2SettingsFrame = {
          0x00, 0x00, 0x00,       // length
          0x04,                   // settings type
          0x00,                   // flags
          0x00, 0x00, 0x00, 0x00  // stream identifier
      };
      if (total_bytes_sent_ < int(kEmptyHttp2SettingsFrame.size())) {
        int bytes_to_send = kEmptyHttp2SettingsFrame.size() - total_bytes_sent_;
        int bytes_sent =
            send(fd_, kEmptyHttp2SettingsFrame.data() + total_bytes_sent_,
                 bytes_to_send, 0);
        if (bytes_sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          gpr_log(GPR_ERROR,
                  "Fake TCP server encountered unexpected error:%d |%s| "
                  "sending %d bytes on fd:%d",
                  errno, strerror(errno), bytes_to_send, fd_);
          GPR_ASSERT(0);
        } else if (bytes_sent > 0) {
          total_bytes_sent_ += bytes_sent;
          GPR_ASSERT(total_bytes_sent_ <= int(kEmptyHttp2SettingsFrame.size()));
        }
      }
    }

    int fd() { return fd_; }

   private:
    int fd_;
    int total_bytes_sent_ = 0;
  };

  // Run a loop that periodically, every 10 ms:
  //   1) Checks if there are any new TCP connections to accept.
  //   2) Checks if any data has arrived yet on established connections,
  //      and reads from them if so, processing the sockets as configured.
  static void RunServerLoop(FakeTcpServer* self) {
    std::set<std::unique_ptr<FakeTcpServerPeer>> peers;
    while (!gpr_event_get(&self->stop_ev_)) {
      int p = accept(self->accept_socket_, nullptr, nullptr);
      if (p == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        gpr_log(GPR_ERROR, "Failed to accept connection: %d", errno);
        abort();
      }
      if (p != -1) {
        gpr_log(GPR_DEBUG, "accepted peer socket: %d", p);
        if (fcntl(p, F_SETFL, O_NONBLOCK) != 0) {
          gpr_log(GPR_ERROR,
                  "Failed to set O_NONBLOCK on peer socket:%d errno:%d", p,
                  errno);
          abort();
        }
        peers.insert(absl::make_unique<FakeTcpServerPeer>(p));
      }
      auto it = peers.begin();
      while (it != peers.end()) {
        FakeTcpServerPeer* peer = (*it).get();
        if (self->accept_mode_ == AcceptMode::kEagerlySendSettings) {
          peer->MaybeContinueSendingSettings();
        }
        char buf[100];
        int bytes_received_size = recv(peer->fd(), buf, 100, 0);
        ProcessReadResult r =
            self->process_read_cb_(bytes_received_size, errno, peer->fd());
        if (r == CLOSE_SOCKET) {
          it = peers.erase(it);
        } else {
          GPR_ASSERT(r == CONTINUE_READING);
          it++;
        }
      }
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_millis(10, GPR_TIMESPAN)));
    }
    close(self->accept_socket_);
  }

 private:
  int accept_socket_;
  int port_;
  gpr_event stop_ev_;
  std::string address_;
  std::unique_ptr<std::thread> run_server_loop_thd_;
  const AcceptMode accept_mode_;
  std::function<ProcessReadResult(int, int, int)> process_read_cb_;
};

/* This test is intended to make sure that ALTS handshakes we correctly
 * fail fast when the security handshaker gets an error while reading
 * from the remote peer, after having earlier sent the first bytes of the
 * ALTS handshake to the peer, i.e. after getting into the middle of a
 * handshake. */
TEST(AltsConcurrentConnectivityTest,
     TestHandshakeFailsFastWhenPeerEndpointClosesConnectionAfterAccepting) {
  // Don't enforce the number of concurrent rpcs for the fake handshake
  // server in this test, because this test will involve handshake RPCs
  // getting cancelled. Because there isn't explicit synchronization between
  // an ALTS handshake client's RECV_STATUS op completing after call
  // cancellation, and the corresponding fake handshake server's sync
  // method handler returning, enforcing a limit on the number of active
  // RPCs at the fake handshake server would be inherently racey.
  FakeHandshakeServer fake_handshake_server(
      false /* check num concurrent rpcs */);
  // The fake_backend_server emulates a secure (ALTS based) gRPC backend. So
  // it waits for the client to send the first bytes.
  FakeTcpServer fake_backend_server(
      FakeTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
      FakeTcpServer::CloseSocketUponReceivingBytesFromPeer);
  {
    gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(20);
    std::vector<std::unique_ptr<ConnectLoopRunner>> connect_loop_runners;
    size_t num_concurrent_connects = 100;
    gpr_log(GPR_DEBUG, "start performing concurrent expected-to-fail connects");
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      connect_loop_runners.push_back(absl::make_unique<ConnectLoopRunner>(
          fake_backend_server.address(), fake_handshake_server.address(),
          10 /* per connect deadline seconds */, 3 /* loops */,
          GRPC_CHANNEL_TRANSIENT_FAILURE /* expected connectivity states */,
          0 /* reconnect_backoff_ms unset */));
    }
    connect_loop_runners.clear();
    gpr_log(GPR_DEBUG, "done performing concurrent expected-to-fail connects");
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), test_deadline) > 0) {
      gpr_log(GPR_ERROR,
              "Exceeded test deadline. ALTS handshakes might not be failing "
              "fast when the peer endpoint closes the connection abruptly");
      abort();
    }
  }
}

/* This test is intended to make sure that ALTS handshakes correctly
 * fail fast when the ALTS handshake server fails incoming handshakes fast. */
TEST(AltsConcurrentConnectivityTest,
     TestHandshakeFailsFastWhenHandshakeServerClosesConnectionAfterAccepting) {
  // The fake_handshake_server emulates a broken ALTS handshaker, which
  // is an insecure server. So send settings to the client eagerly.
  FakeTcpServer fake_handshake_server(
      FakeTcpServer::AcceptMode::kEagerlySendSettings,
      FakeTcpServer::CloseSocketUponReceivingBytesFromPeer);
  // The fake_backend_server emulates a secure (ALTS based) server, so wait
  // for the client to send the first bytes.
  FakeTcpServer fake_backend_server(
      FakeTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
      FakeTcpServer::CloseSocketUponCloseFromPeer);
  {
    gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(20);
    std::vector<std::unique_ptr<ConnectLoopRunner>> connect_loop_runners;
    size_t num_concurrent_connects = 100;
    gpr_log(GPR_DEBUG, "start performing concurrent expected-to-fail connects");
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      connect_loop_runners.push_back(absl::make_unique<ConnectLoopRunner>(
          fake_backend_server.address(), fake_handshake_server.address(),
          20 /* per connect deadline seconds */, 2 /* loops */,
          GRPC_CHANNEL_TRANSIENT_FAILURE /* expected connectivity states */,
          0 /* reconnect_backoff_ms unset */));
    }
    connect_loop_runners.clear();
    gpr_log(GPR_DEBUG, "done performing concurrent expected-to-fail connects");
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), test_deadline) > 0) {
      gpr_log(GPR_ERROR,
              "Exceeded test deadline. ALTS handshakes might not be failing "
              "fast when the handshake server closes new connections");
      abort();
    }
  }
}

/* This test is intended to make sure that ALTS handshakes correctly
 * fail fast when the ALTS handshake server is non-responsive, in which case
 * the overall connection deadline kicks in. */
TEST(AltsConcurrentConnectivityTest,
     TestHandshakeFailsFastWhenHandshakeServerHangsAfterAccepting) {
  // fake_handshake_server emulates an insecure server, so send settings first.
  // It will be unresponsive for the rest of the connection, though.
  FakeTcpServer fake_handshake_server(
      FakeTcpServer::AcceptMode::kEagerlySendSettings,
      FakeTcpServer::CloseSocketUponCloseFromPeer);
  // fake_backend_server emulates an ALTS based server, so wait for the client
  // to send the first bytes.
  FakeTcpServer fake_backend_server(
      FakeTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
      FakeTcpServer::CloseSocketUponCloseFromPeer);
  {
    gpr_timespec test_deadline = grpc_timeout_seconds_to_deadline(20);
    std::vector<std::unique_ptr<ConnectLoopRunner>> connect_loop_runners;
    size_t num_concurrent_connects = 100;
    gpr_log(GPR_DEBUG, "start performing concurrent expected-to-fail connects");
    for (size_t i = 0; i < num_concurrent_connects; i++) {
      connect_loop_runners.push_back(absl::make_unique<ConnectLoopRunner>(
          fake_backend_server.address(), fake_handshake_server.address(),
          10 /* per connect deadline seconds */, 2 /* loops */,
          GRPC_CHANNEL_TRANSIENT_FAILURE /* expected connectivity states */,
          100 /* reconnect_backoff_ms */));
    }
    connect_loop_runners.clear();
    gpr_log(GPR_DEBUG, "done performing concurrent expected-to-fail connects");
    if (gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), test_deadline) > 0) {
      gpr_log(GPR_ERROR,
              "Exceeded test deadline. ALTS handshakes might not be failing "
              "fast when the handshake server is non-response timeout occurs");
      abort();
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
