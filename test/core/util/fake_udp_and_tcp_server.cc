//
// Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "test/core/util/fake_udp_and_tcp_server.h"

#include <errno.h>
#include <string.h>

#include <set>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/util/port.h"

// IWYU pragma: no_include <arpa/inet.h>

#ifdef GPR_WINDOWS
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#include "src/core/lib/iomgr/tcp_windows.h"

#define BAD_SOCKET_RETURN_VAL INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#define ERRNO WSAGetLastError()
#else
#include <fcntl.h>
#include <unistd.h>

#define BAD_SOCKET_RETURN_VAL (-1)
#define CLOSE_SOCKET close
#define ERRNO errno
#endif

namespace grpc_core {
namespace testing {

namespace {

bool ErrorIsRetryable(int error) {
#ifdef GPR_WINDOWS
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
  return error == EWOULDBLOCK || error == EAGAIN;
#endif
}

}  // namespace

FakeUdpAndTcpServer::FakeUdpAndTcpServer(
    AcceptMode accept_mode,
    std::function<FakeUdpAndTcpServer::ProcessReadResult(int, int, int)>
        process_read_cb)
    : accept_mode_(accept_mode), process_read_cb_(std::move(process_read_cb)) {
  port_ = grpc_pick_unused_port_or_die();
  udp_socket_ = socket(AF_INET6, SOCK_DGRAM, 0);
  if (udp_socket_ == BAD_SOCKET_RETURN_VAL) {
    gpr_log(GPR_ERROR, "Failed to create UDP ipv6 socket: %d", ERRNO);
    GPR_ASSERT(0);
  }
  accept_socket_ = socket(AF_INET6, SOCK_STREAM, 0);
  address_ = absl::StrCat("[::1]:", port_);
  if (accept_socket_ == BAD_SOCKET_RETURN_VAL) {
    gpr_log(GPR_ERROR, "Failed to create TCP IPv6 socket: %d", ERRNO);
    GPR_ASSERT(0);
  }
#ifdef GPR_WINDOWS
  char val = 1;
  if (setsockopt(accept_socket_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) ==
      SOCKET_ERROR) {
    gpr_log(GPR_ERROR,
            "Failed to set SO_REUSEADDR on TCP ipv6 socket to [::1]:%d, "
            "errno: %d",
            port_, ERRNO);
    GPR_ASSERT(0);
  }
  grpc_error_handle set_non_block_error;
  set_non_block_error = grpc_tcp_set_non_block(udp_socket_);
  if (!set_non_block_error.ok()) {
    gpr_log(GPR_ERROR, "Failed to configure non-blocking socket: %s",
            StatusToString(set_non_block_error).c_str());
    GPR_ASSERT(0);
  }
  set_non_block_error = grpc_tcp_set_non_block(accept_socket_);
  if (!set_non_block_error.ok()) {
    gpr_log(GPR_ERROR, "Failed to configure non-blocking socket: %s",
            StatusToString(set_non_block_error).c_str());
    GPR_ASSERT(0);
  }
#else
  int val = 1;
  if (setsockopt(accept_socket_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) !=
      0) {
    gpr_log(GPR_ERROR, "Failed to set SO_REUSEADDR on socket [::1]:%d", port_);
    GPR_ASSERT(0);
  }
  if (fcntl(udp_socket_, F_SETFL, O_NONBLOCK) != 0) {
    gpr_log(GPR_ERROR, "Failed to set O_NONBLOCK on socket: %d", ERRNO);
    GPR_ASSERT(0);
  }
  if (fcntl(accept_socket_, F_SETFL, O_NONBLOCK) != 0) {
    gpr_log(GPR_ERROR, "Failed to set O_NONBLOCK on socket: %d", ERRNO);
    GPR_ASSERT(0);
  }
#endif
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port_);
  (reinterpret_cast<char*>(&addr.sin6_addr))[15] = 1;
  grpc_resolved_address resolved_addr;
  memcpy(resolved_addr.addr, &addr, sizeof(addr));
  resolved_addr.len = sizeof(addr);
  std::string addr_str = grpc_sockaddr_to_string(&resolved_addr, false).value();
  gpr_log(GPR_INFO, "Fake UDP and TCP server listening on %s",
          addr_str.c_str());
  if (bind(udp_socket_, reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    gpr_log(GPR_ERROR, "Failed to bind UDP socket to [::1]:%d", port_);
    GPR_ASSERT(0);
  }
  if (bind(accept_socket_, reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    gpr_log(GPR_ERROR, "Failed to bind TCP socket to [::1]:%d : %d", port_,
            ERRNO);
    GPR_ASSERT(0);
  }
  if (listen(accept_socket_, 100)) {
    gpr_log(GPR_ERROR, "Failed to listen on socket bound to [::1]:%d : %d",
            port_, ERRNO);
    GPR_ASSERT(0);
  }
  gpr_event_init(&stop_ev_);
  run_server_loop_thd_ = std::make_unique<std::thread>(
      std::bind(&FakeUdpAndTcpServer::RunServerLoop, this));
}

FakeUdpAndTcpServer::~FakeUdpAndTcpServer() {
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

FakeUdpAndTcpServer::ProcessReadResult
FakeUdpAndTcpServer::CloseSocketUponReceivingBytesFromPeer(
    int bytes_received_size, int read_error, int s) {
  if (bytes_received_size < 0 && !ErrorIsRetryable(read_error)) {
    gpr_log(GPR_ERROR, "Failed to receive from peer socket: %d. errno: %d", s,
            read_error);
    GPR_ASSERT(0);
  }
  if (bytes_received_size >= 0) {
    gpr_log(GPR_DEBUG,
            "Fake TCP server received %d bytes from peer socket: %d. Close "
            "the "
            "connection.",
            bytes_received_size, s);
    return FakeUdpAndTcpServer::ProcessReadResult::kCloseSocket;
  }
  return FakeUdpAndTcpServer::ProcessReadResult::kContinueReading;
}

FakeUdpAndTcpServer::ProcessReadResult
FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer(int bytes_received_size,
                                                  int read_error, int s) {
  if (bytes_received_size < 0 && !ErrorIsRetryable(read_error)) {
    gpr_log(GPR_ERROR, "Failed to receive from peer socket: %d. errno: %d", s,
            read_error);
    GPR_ASSERT(0);
  }
  if (bytes_received_size == 0) {
    // The peer has shut down the connection.
    gpr_log(GPR_DEBUG,
            "Fake TCP server received 0 bytes from peer socket: %d. Close "
            "the "
            "connection.",
            s);
    return FakeUdpAndTcpServer::ProcessReadResult::kCloseSocket;
  }
  return FakeUdpAndTcpServer::ProcessReadResult::kContinueReading;
}

FakeUdpAndTcpServer::FakeUdpAndTcpServerPeer::FakeUdpAndTcpServerPeer(int fd)
    : fd_(fd) {}

FakeUdpAndTcpServer::FakeUdpAndTcpServerPeer::~FakeUdpAndTcpServerPeer() {
  CLOSE_SOCKET(fd_);
}

void FakeUdpAndTcpServer::FakeUdpAndTcpServerPeer::
    MaybeContinueSendingSettings() {
  // https://tools.ietf.org/html/rfc7540#section-4.1
  const std::vector<char> kEmptyHttp2SettingsFrame = {
      0x00, 0x00, 0x00,       // length
      0x04,                   // settings type
      0x00,                   // flags
      0x00, 0x00, 0x00, 0x00  // stream identifier
  };
  if (total_bytes_sent_ < static_cast<int>(kEmptyHttp2SettingsFrame.size())) {
    int bytes_to_send = kEmptyHttp2SettingsFrame.size() - total_bytes_sent_;
    int bytes_sent =
        send(fd_, kEmptyHttp2SettingsFrame.data() + total_bytes_sent_,
             bytes_to_send, 0);
    if (bytes_sent < 0 && !ErrorIsRetryable(ERRNO)) {
      gpr_log(GPR_ERROR,
              "Fake TCP server encountered unexpected error:%d "
              "sending %d bytes on fd:%d",
              ERRNO, bytes_to_send, fd_);
      GPR_ASSERT(0);
    } else if (bytes_sent > 0) {
      total_bytes_sent_ += bytes_sent;
      GPR_ASSERT(total_bytes_sent_ <= int(kEmptyHttp2SettingsFrame.size()));
    }
  }
}

void FakeUdpAndTcpServer::ReadFromUdpSocket() {
  char buf[100];
  recvfrom(udp_socket_, buf, sizeof(buf), 0, nullptr, nullptr);
}

void FakeUdpAndTcpServer::RunServerLoop() {
  std::set<std::unique_ptr<FakeUdpAndTcpServerPeer>> peers;
  while (!gpr_event_get(&stop_ev_)) {
    // handle TCP connections
    int p = accept(accept_socket_, nullptr, nullptr);
    if (p != BAD_SOCKET_RETURN_VAL) {
      gpr_log(GPR_DEBUG, "accepted peer socket: %d", p);
#ifdef GPR_WINDOWS
      grpc_error_handle set_non_block_error;
      set_non_block_error = grpc_tcp_set_non_block(p);
      if (!set_non_block_error.ok()) {
        gpr_log(GPR_ERROR, "Failed to configure non-blocking socket: %s",
                StatusToString(set_non_block_error).c_str());
        GPR_ASSERT(0);
      }
#else
      if (fcntl(p, F_SETFL, O_NONBLOCK) != 0) {
        gpr_log(GPR_ERROR, "Failed to configure non-blocking socket, errno: %d",
                ERRNO);
        GPR_ASSERT(0);
      }
#endif
      peers.insert(std::make_unique<FakeUdpAndTcpServerPeer>(p));
    }
    auto it = peers.begin();
    while (it != peers.end()) {
      FakeUdpAndTcpServerPeer* peer = (*it).get();
      if (accept_mode_ == AcceptMode::kEagerlySendSettings) {
        peer->MaybeContinueSendingSettings();
      }
      char buf[100];
      int bytes_received_size = recv(peer->fd(), buf, 100, 0);
      FakeUdpAndTcpServer::ProcessReadResult r =
          process_read_cb_(bytes_received_size, ERRNO, peer->fd());
      if (r == FakeUdpAndTcpServer::ProcessReadResult::kCloseSocket) {
        it = peers.erase(it);
      } else {
        GPR_ASSERT(r ==
                   FakeUdpAndTcpServer::ProcessReadResult::kContinueReading);
        it++;
      }
    }
    // read from the UDP socket
    ReadFromUdpSocket();
    gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                 gpr_time_from_millis(10, GPR_TIMESPAN)));
  }
}

}  // namespace testing
}  // namespace grpc_core
