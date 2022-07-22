// Copyright 2022 gRPC authors.
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
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/windows/iocp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/time/time.h"

#include <grpc/grpc.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/event_engine/windows/socket.h"
#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/event_engine/windows/create_sockpair.h"

namespace {
using ::grpc_event_engine::experimental::CreateSockpair;
using ::grpc_event_engine::experimental::IOCP;
using ::grpc_event_engine::experimental::WindowsEventEngine;
using ::grpc_event_engine::experimental::WinWrappedSocket;
}  // namespace

class IOCPTest : public testing::Test {};

TEST_F(IOCPTest, ClientReceivesNotificationOfServerSendViaIOCP) {
  auto engine = absl::make_unique<WindowsEventEngine>();
  IOCP iocp(engine.get());
  SOCKET sockpair[2];
  CreateSockpair(sockpair, iocp.GetDefaultSocketFlags());
  WinWrappedSocket* wrapped_client_socket =
      static_cast<WinWrappedSocket*>(iocp.Watch(sockpair[0]));
  WinWrappedSocket* wrapped_server_socket =
      static_cast<WinWrappedSocket*>(iocp.Watch(sockpair[1]));
  bool read_called = false;
  bool write_called = false;
  DWORD flags = 0;
  {
    // When the client gets some data, ensure it matches what we expect.
    WSABUF read_wsabuf;
    read_wsabuf.len = 2048;
    char read_char_buffer[2048];
    read_wsabuf.buf = read_char_buffer;
    DWORD bytes_rcvd;
    memset(wrapped_client_socket->read_info()->overlapped(), 0,
           sizeof(OVERLAPPED));
    int status =
        WSARecv(wrapped_client_socket->Socket(), &read_wsabuf, 1, &bytes_rcvd,
                &flags, wrapped_client_socket->read_info()->overlapped(), NULL);
    // Expecting error 997, WSA_IO_PENDING
    EXPECT_EQ(status, -1);
    int last_error = WSAGetLastError();
    ASSERT_EQ(last_error, WSA_IO_PENDING);
    wrapped_client_socket->NotifyOnRead([wrapped_client_socket, &read_called,
                                         &read_wsabuf, &bytes_rcvd]() {
      gpr_log(GPR_DEBUG, "Notified on read");
      EXPECT_GE(wrapped_client_socket->read_info()->bytes_transferred(), 10);
      EXPECT_STREQ(read_wsabuf.buf, "hello!");
      read_called = true;
    });
  }
  {
    // Have the server send a message to the client
    WSABUF write_wsabuf;
    char write_char_buffer[2048] = "hello!";
    write_wsabuf.len = 2048;
    write_wsabuf.buf = write_char_buffer;
    DWORD bytes_sent;
    memset(wrapped_server_socket->write_info()->overlapped(), 0,
           sizeof(OVERLAPPED));
    int status =
        WSASend(wrapped_server_socket->Socket(), &write_wsabuf, 1, &bytes_sent,
                0, wrapped_server_socket->write_info()->overlapped(), NULL);
    EXPECT_EQ(status, 0);
    if (status != 0) {
      int error_num = WSAGetLastError();
      char* utf8_message = gpr_format_message(error_num);
      gpr_log(GPR_INFO, "Error sending data: (%d) %s", error_num, utf8_message);
      gpr_free(utf8_message);
    }
    wrapped_server_socket->NotifyOnWrite([&write_called] {
      gpr_log(GPR_DEBUG, "Notified on write");
      write_called = true;
    });
  }
  // Working for WSASend
  ASSERT_TRUE(iocp.Work(std::chrono::seconds(10)).ok());
  // Working for WSARecv
  ASSERT_TRUE(iocp.Work(std::chrono::seconds(10)).ok());
  absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (!read_called || !write_called) {
    absl::SleepFor(absl::Milliseconds(10));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded";
    }
  }
  ASSERT_TRUE(read_called);
  ASSERT_TRUE(write_called);

  delete wrapped_client_socket;
  delete wrapped_server_socket;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int status = RUN_ALL_TESTS();
  grpc_shutdown();
  return status;
}
