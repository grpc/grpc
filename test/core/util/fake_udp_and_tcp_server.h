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

#include <functional>
#include <set>
#include <thread>

#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/sockaddr.h"
#include "test/core/util/port.h"

#ifdef GPR_WINDOWS
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"
#define BAD_SOCKET_RETURN_VAL INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <fcntl.h>

#include "src/core/lib/iomgr/sockaddr_posix.h"
#define BAD_SOCKET_RETURN_VAL (-1)
#define CLOSE_SOCKET close
#endif

namespace grpc_core {
namespace testing {

class FakeUdpAndTcpServer {
 public:
  enum class ProcessReadResult {
    kContinueReading = 0,
    kCloseSocket,
  };

  enum class AcceptMode {
    kWaitForClientToSendFirstBytes,  // useful for emulating ALTS based
                                     // grpc servers
    kEagerlySendSettings,  // useful for emulating insecure grpc servers (e.g.
                           // ALTS handshake servers)
  };

  explicit FakeUdpAndTcpServer(
      AcceptMode accept_mode,
      const std::function<ProcessReadResult(int, int, int)>& process_read_cb)
      : accept_mode_(accept_mode), process_read_cb_(process_read_cb) {
    port_ = grpc_pick_unused_port_or_die();
    udp_socket_ = socket(AF_INET6, SOCK_DGRAM, 0);
    if (udp_socket_ == BAD_SOCKET_RETURN_VAL) {
      gpr_log(GPR_DEBUG, "Failed to create UDP ipv6 socket: %d", errno);
      GPR_ASSERT(0);
    }
    accept_socket_ = socket(AF_INET6, SOCK_STREAM, 0);
    address_ = absl::StrCat("[::]:", port_);
    if (accept_socket_ == BAD_SOCKET_RETURN_VAL) {
      gpr_log(GPR_ERROR, "Failed to create TCP IPv6 socket: %d", errno);
      GPR_ASSERT(0);
    }
#ifdef GPR_WINDOWS
    char val = 1;
    if (setsockopt(accept_socket_, SOL_SOCKET, SO_REUSEADDR, &val,
                   sizeof(val)) == SOCKET_ERROR) {
      gpr_log(GPR_DEBUG,
              "Failed to set SO_REUSEADDR on TCP ipv6 socket to [::1]:%d, "
              "errno: %d",
              port_, errno);
      GPR_ASSERT(0);
    }
    grpc_error_handle set_non_block_error;
    set_non_block_error = grpc_tcp_set_non_block(udp_socket_);
    if (set_non_block_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Failed to configure non-blocking socket: %s",
              grpc_error_std_string(set_non_block_error).c_str());
      GPR_ASSERT(0);
    }
    set_non_block_error = grpc_tcp_set_non_block(accept_socket_);
    if (set_non_block_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR, "Failed to configure non-blocking socket: %s",
              grpc_error_std_string(set_non_block_error).c_str());
      GPR_ASSERT(0);
    }
#else
    int val = 1;
    if (setsockopt(accept_socket_, SOL_SOCKET, SO_REUSEADDR, &val,
                   sizeof(val)) != 0) {
      gpr_log(GPR_DEBUG, "Failed to set SO_REUSEADDR on socket [::1]:%d",
              port_);
      GPR_ASSERT(0);
    }
    if (fcntl(udp_socket_, F_SETFL, O_NONBLOCK) != 0) {
      gpr_log(GPR_ERROR, "Failed to set O_NONBLOCK on socket: %d", errno);
      GPR_ASSERT(0);
    }
    if (fcntl(accept_socket_, F_SETFL, O_NONBLOCK) != 0) {
      gpr_log(GPR_ERROR, "Failed to set O_NONBLOCK on socket: %d", errno);
      GPR_ASSERT(0);
    }
#endif
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port_);
    (reinterpret_cast<char*>(&addr.sin6_addr))[15] = 1;
    if (bind(udp_socket_, reinterpret_cast<const sockaddr*>(&addr),
             sizeof(addr)) != 0) {
      gpr_log(GPR_DEBUG, "Failed to bind UDP socket to [::1]:%d", port_);
      GPR_ASSERT(0);
    }
    if (bind(accept_socket_, reinterpret_cast<const sockaddr*>(&addr),
             sizeof(addr)) != 0) {
      gpr_log(GPR_ERROR, "Failed to bind TCP socket to [::1]:%d : %d", port_,
              errno);
      GPR_ASSERT(0);
    }
    if (listen(accept_socket_, 100)) {
      gpr_log(GPR_ERROR, "Failed to listen on socket bound to [::1]:%d : %d",
              port_, errno);
      GPR_ASSERT(0);
    }
    gpr_event_init(&stop_ev_);
    run_server_loop_thd_ = absl::make_unique<std::thread>(RunServerLoop, this);
  }

  ~FakeUdpAndTcpServer() {
    gpr_log(GPR_DEBUG,
            "FakeUdpAndTcpServer stop and "
            "join server thread");
    gpr_event_set(&stop_ev_, reinterpret_cast<void*>(1));
    run_server_loop_thd_->join();
    gpr_log(GPR_DEBUG,
            "FakeUdpAndTcpServer join server "
            "thread complete");
    CLOSE_SOCKET(accept_socket_);
    CLOSE_SOCKET(udp_socket_);
  }

  const char* address() { return address_.c_str(); }

  int port() { return port_; };

  static ProcessReadResult CloseSocketUponReceivingBytesFromPeer(
      int bytes_received_size, int read_error, int s) {
    if (bytes_received_size < 0 && read_error != EAGAIN &&
        read_error != EWOULDBLOCK) {
      gpr_log(GPR_ERROR, "Failed to receive from peer socket: %d. errno: %d", s,
              errno);
      GPR_ASSERT(0);
    }
    if (bytes_received_size >= 0) {
      gpr_log(GPR_DEBUG,
              "Fake TCP server received %d bytes from peer socket: %d. Close "
              "the "
              "connection.",
              bytes_received_size, s);
      return ProcessReadResult::kCloseSocket;
    }
    return ProcessReadResult::kContinueReading;
  }

  static ProcessReadResult CloseSocketUponCloseFromPeer(int bytes_received_size,
                                                        int read_error, int s) {
    if (bytes_received_size < 0 && read_error != EAGAIN &&
        read_error != EWOULDBLOCK) {
      gpr_log(GPR_ERROR, "Failed to receive from peer socket: %d. errno: %d", s,
              errno);
      GPR_ASSERT(0);
    }
    if (bytes_received_size == 0) {
      // The peer has shut down the connection.
      gpr_log(GPR_DEBUG,
              "Fake TCP server received 0 bytes from peer socket: %d. Close "
              "the "
              "connection.",
              s);
      return ProcessReadResult::kCloseSocket;
    }
    return ProcessReadResult::kContinueReading;
  }

  class FakeUdpAndTcpServerPeer {
   public:
    explicit FakeUdpAndTcpServerPeer(int fd) : fd_(fd) {}

    ~FakeUdpAndTcpServerPeer() { CLOSE_SOCKET(fd_); }

    void MaybeContinueSendingSettings() {
      // https://tools.ietf.org/html/rfc7540#section-4.1
      const std::vector<char> kEmptyHttp2SettingsFrame = {
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

  void ReadFromUdpSocket() {
    char buf[100];
    recvfrom(udp_socket_, buf, sizeof(buf), 0, nullptr, nullptr);
  }

  // Run a loop that periodically, every 10 ms:
  //   1) Checks if there are any new TCP connections to accept.
  //   2) Checks if any data has arrived yet on established connections,
  //      and reads from them if so, processing the sockets as configured.
  static void RunServerLoop(FakeUdpAndTcpServer* self) {
    std::set<std::unique_ptr<FakeUdpAndTcpServerPeer>> peers;
    while (!gpr_event_get(&self->stop_ev_)) {
      // handle TCP connections
      int p = accept(self->accept_socket_, nullptr, nullptr);
      if (p != BAD_SOCKET_RETURN_VAL) {
        gpr_log(GPR_DEBUG, "accepted peer socket: %d", p);
#ifdef GPR_WINDOWS
        grpc_error_handle set_non_block_error;
        set_non_block_error = grpc_tcp_set_non_block(p);
        if (set_non_block_error != GRPC_ERROR_NONE) {
          gpr_log(GPR_ERROR, "Failed to configure non-blocking socket: %s",
                  grpc_error_std_string(set_non_block_error).c_str());
          GPR_ASSERT(0);
        }
#else
        if (fcntl(p, F_SETFL, O_NONBLOCK) != 0) {
          gpr_log(GPR_ERROR,
                  "Failed to configure non-blocking socket, errno: %d", errno);
          GPR_ASSERT(0);
        }
#endif
        peers.insert(absl::make_unique<FakeUdpAndTcpServerPeer>(p));
      }
      auto it = peers.begin();
      while (it != peers.end()) {
        FakeUdpAndTcpServerPeer* peer = (*it).get();
        if (self->accept_mode_ == AcceptMode::kEagerlySendSettings) {
          peer->MaybeContinueSendingSettings();
        }
        char buf[100];
        int bytes_received_size = recv(peer->fd(), buf, 100, 0);
        ProcessReadResult r =
            self->process_read_cb_(bytes_received_size, errno, peer->fd());
        if (r == ProcessReadResult::kCloseSocket) {
          it = peers.erase(it);
        } else {
          GPR_ASSERT(r == ProcessReadResult::kContinueReading);
          it++;
        }
      }
      // read from the UDP socket
      self->ReadFromUdpSocket();
      gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_millis(10, GPR_TIMESPAN)));
    }
  }

 private:
  int accept_socket_;
  int udp_socket_;
  int port_;
  gpr_event stop_ev_;
  std::string address_;
  std::unique_ptr<std::thread> run_server_loop_thd_;
  const AcceptMode accept_mode_;
  std::function<ProcessReadResult(int, int, int)> process_read_cb_;
};

}  // namespace testing
}  // namespace grpc_core
