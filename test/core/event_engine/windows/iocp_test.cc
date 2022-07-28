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

#ifdef GPR_WINDOWS
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "absl/types/variant.h"

#include <grpc/grpc.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/executor/threaded_executor.h"
#include "src/core/lib/event_engine/poller.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/event_engine/windows/create_sockpair.h"

// DO NOT SUBMIT(hork): get sanitizers working

namespace {
using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::CreateSockpair;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::IOCP;
using ::grpc_event_engine::experimental::Poller;
using ::grpc_event_engine::experimental::SelfDeletingClosure;
using ::grpc_event_engine::experimental::ThreadedExecutor;
using ::grpc_event_engine::experimental::WinSocket;
}  // namespace

class IOCPTest : public testing::Test {};

TEST_F(IOCPTest, ClientReceivesNotificationOfServerSend) {
  ThreadedExecutor executor{2};
  IOCP iocp(&executor);
  SOCKET sockpair[2];
  CreateSockpair(sockpair, iocp.GetDefaultSocketFlags());
  WinSocket* wrapped_client_socket =
      static_cast<WinSocket*>(iocp.Watch(sockpair[0]));
  WinSocket* wrapped_server_socket =
      static_cast<WinSocket*>(iocp.Watch(sockpair[1]));
  bool read_called = false;
  bool write_called = false;
  DWORD flags = 0;
  AnyInvocableClosure* on_read;
  AnyInvocableClosure* on_write;
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
        WSARecv(wrapped_client_socket->socket(), &read_wsabuf, 1, &bytes_rcvd,
                &flags, wrapped_client_socket->read_info()->overlapped(), NULL);
    // Expecting error 997, WSA_IO_PENDING
    EXPECT_EQ(status, -1);
    int last_error = WSAGetLastError();
    ASSERT_EQ(last_error, WSA_IO_PENDING);
    on_read = new AnyInvocableClosure([wrapped_client_socket, &read_called,
                                       &read_wsabuf, &bytes_rcvd]() {
      gpr_log(GPR_DEBUG, "Notified on read");
      EXPECT_GE(wrapped_client_socket->read_info()->bytes_transferred(), 10);
      EXPECT_STREQ(read_wsabuf.buf, "hello!");
      read_called = true;
    });
    wrapped_client_socket->NotifyOnRead(on_read);
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
        WSASend(wrapped_server_socket->socket(), &write_wsabuf, 1, &bytes_sent,
                0, wrapped_server_socket->write_info()->overlapped(), NULL);
    EXPECT_EQ(status, 0);
    if (status != 0) {
      int error_num = WSAGetLastError();
      char* utf8_message = gpr_format_message(error_num);
      gpr_log(GPR_INFO, "Error sending data: (%d) %s", error_num, utf8_message);
      gpr_free(utf8_message);
    }
    on_write = new AnyInvocableClosure([&write_called] {
      gpr_log(GPR_DEBUG, "Notified on write");
      write_called = true;
    });
    wrapped_server_socket->NotifyOnWrite(on_write);
  }
  // Doing work for WSASend
  auto work_result = iocp.Work(std::chrono::seconds(10));
  ASSERT_TRUE(absl::holds_alternative<Poller::Events>(work_result));
  Poller::Events closures = absl::get<Poller::Events>(work_result);
  ASSERT_EQ(closures.size(), 1);
  executor.Run(closures[0]);
  // Doing work for WSARecv
  work_result = iocp.Work(std::chrono::seconds(10));
  ASSERT_TRUE(absl::holds_alternative<Poller::Events>(work_result));
  closures = absl::get<Poller::Events>(work_result);
  ASSERT_EQ(closures.size(), 1);
  executor.Run(closures[0]);
  // wait for the callbacks to run
  absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (!read_called || !write_called) {
    absl::SleepFor(absl::Milliseconds(10));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded";
    }
  }
  // redundant, but self-documenting
  ASSERT_TRUE(read_called);
  ASSERT_TRUE(write_called);

  delete on_read;
  delete on_write;
  wrapped_client_socket->MaybeShutdown(absl::OkStatus());
  wrapped_server_socket->MaybeShutdown(absl::OkStatus());
  delete wrapped_client_socket;
  delete wrapped_server_socket;
}

TEST_F(IOCPTest, IocpWorkTimeoutDueToNoNotificationRegistered) {
  ThreadedExecutor executor{2};
  IOCP iocp(&executor);
  SOCKET sockpair[2];
  CreateSockpair(sockpair, iocp.GetDefaultSocketFlags());
  WinSocket* wrapped_client_socket =
      static_cast<WinSocket*>(iocp.Watch(sockpair[0]));
  bool read_called = false;
  DWORD flags = 0;
  AnyInvocableClosure* on_read;
  {
    // Set the client to receive asynchronously
    // Prepare a notification callback, but don't register it yet.
    WSABUF read_wsabuf;
    read_wsabuf.len = 2048;
    char read_char_buffer[2048];
    read_wsabuf.buf = read_char_buffer;
    DWORD bytes_rcvd;
    memset(wrapped_client_socket->read_info()->overlapped(), 0,
           sizeof(OVERLAPPED));
    int status =
        WSARecv(wrapped_client_socket->socket(), &read_wsabuf, 1, &bytes_rcvd,
                &flags, wrapped_client_socket->read_info()->overlapped(), NULL);
    // Expecting error 997, WSA_IO_PENDING
    EXPECT_EQ(status, -1);
    int last_error = WSAGetLastError();
    ASSERT_EQ(last_error, WSA_IO_PENDING);
    on_read = new AnyInvocableClosure([wrapped_client_socket, &read_called,
                                       &read_wsabuf, &bytes_rcvd]() {
      gpr_log(GPR_DEBUG, "Notified on read");
      EXPECT_GE(wrapped_client_socket->read_info()->bytes_transferred(), 10);
      EXPECT_STREQ(read_wsabuf.buf, "hello!");
      read_called = true;
    });
  }
  {
    // Have the server send a message to the client. No need to track via IOCP
    WSABUF write_wsabuf;
    char write_char_buffer[2048] = "hello!";
    write_wsabuf.len = 2048;
    write_wsabuf.buf = write_char_buffer;
    DWORD bytes_sent;
    OVERLAPPED write_overlapped;
    memset(&write_overlapped, 0, sizeof(OVERLAPPED));
    int status = WSASend(sockpair[1], &write_wsabuf, 1, &bytes_sent, 0,
                         &write_overlapped, NULL);
    EXPECT_EQ(status, 0);
  }
  // IOCP::Work without any notification callbacks should return no Events.
  auto work_result = iocp.Work(std::chrono::seconds(2));
  ASSERT_TRUE(absl::holds_alternative<Poller::Events>(work_result));
  Poller::Events closures = absl::get<Poller::Events>(work_result);
  ASSERT_EQ(closures.size(), 0);
  // register the closure, which should trigger it immediately.
  wrapped_client_socket->NotifyOnRead(on_read);
  // wait for the callbacks to run
  absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (!read_called) {
    absl::SleepFor(absl::Milliseconds(10));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded";
    }
  }
  ASSERT_TRUE(read_called);

  delete on_read;
  wrapped_client_socket->MaybeShutdown(absl::OkStatus());
  delete wrapped_client_socket;
}

TEST_F(IOCPTest, KickWorks) {
  ThreadedExecutor executor{2};
  IOCP iocp(&executor);
  bool kicked;
  executor.Run([&iocp, &kicked] {
    Poller::WorkResult result = iocp.Work(std::chrono::seconds(30));
    ASSERT_TRUE(absl::holds_alternative<Poller::Kicked>(result));
    kicked = true;
  });
  executor.Run([&iocp] {
    // give the worker thread a chance to start
    absl::SleepFor(absl::Milliseconds(42));
    iocp.Kick();
  });
  // wait for the callbacks to run
  absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (!kicked) {
    absl::SleepFor(absl::Milliseconds(10));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded";
    }
  }
  ASSERT_TRUE(kicked);
}

TEST_F(IOCPTest, KickThenShutdownCasusesNextWorkerToBeKicked) {
  // This documents the existing poller's behavior of maintaining a kick count,
  // but it's unclear if it's going to be needed.
  // TODO(hork): evaluate if a kick count is going to be useful.
  ThreadedExecutor executor{2};
  IOCP iocp(&executor);
  // kick twice
  iocp.Kick();
  iocp.Kick();
  // Assert the next two WorkResults are kicks
  auto result = iocp.Work(std::chrono::milliseconds(1));
  ASSERT_TRUE(absl::holds_alternative<Poller::Kicked>(result));
  result = iocp.Work(std::chrono::milliseconds(1));
  ASSERT_TRUE(absl::holds_alternative<Poller::Kicked>(result));
  // followed by a DeadlineExceeded
  result = iocp.Work(std::chrono::milliseconds(1));
  ASSERT_TRUE(absl::holds_alternative<Poller::DeadlineExceeded>(result));
}

TEST_F(IOCPTest, CrashOnWatchingAClosedSocket) {
  ThreadedExecutor executor{2};
  IOCP iocp(&executor);
  SOCKET sockpair[2];
  CreateSockpair(sockpair, iocp.GetDefaultSocketFlags());
  closesocket(sockpair[0]);
  ASSERT_DEATH(
      {
        WinSocket* wrapped_client_socket =
            static_cast<WinSocket*>(iocp.Watch(sockpair[0]));
      },
      "");
}

TEST_F(IOCPTest, StressTestThousandsOfSockets) {
  // Start 100 threads, each with their own IOCP
  // On each thread, create 50 socket pairs (100 sockets) and have them exchange
  // a message before shutting down.
  int thread_count = 100;
  int sockets_per_thread = 50;
  std::atomic<int> read_count{0};
  std::atomic<int> write_count{0};
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (int thread_n = 0; thread_n < thread_count; thread_n++) {
    threads.emplace_back([thread_n, sockets_per_thread, &read_count,
                          &write_count] {
      ThreadedExecutor executor{2};
      IOCP iocp(&executor);
      // Start a looping worker thread with a moderate timeout
      std::thread iocp_worker([&iocp, &executor] {
        Poller::WorkResult result;
        do {
          result = iocp.Work(std::chrono::seconds(1));
          if (absl::holds_alternative<Poller::Events>(result)) {
            for (auto& event : absl::get<Poller::Events>(result)) {
              executor.Run(event);
            }
          }
        } while (!absl::holds_alternative<Poller::DeadlineExceeded>(result));
      });
      for (int i = 0; i < sockets_per_thread; i++) {
        SOCKET sockpair[2];
        CreateSockpair(sockpair, iocp.GetDefaultSocketFlags());
        WinSocket* wrapped_client_socket =
            static_cast<WinSocket*>(iocp.Watch(sockpair[0]));
        WinSocket* wrapped_server_socket =
            static_cast<WinSocket*>(iocp.Watch(sockpair[1]));
        wrapped_client_socket->NotifyOnRead(
            SelfDeletingClosure::Create([&read_count, wrapped_client_socket] {
              read_count.fetch_add(1);
              wrapped_client_socket->MaybeShutdown(absl::OkStatus());
            }));
        wrapped_server_socket->NotifyOnWrite(
            SelfDeletingClosure::Create([&write_count, wrapped_server_socket] {
              write_count.fetch_add(1);
              wrapped_server_socket->MaybeShutdown(absl::OkStatus());
            }));
        {
          // Set the client to receive
          WSABUF read_wsabuf;
          read_wsabuf.len = 20;
          char read_char_buffer[20];
          read_wsabuf.buf = read_char_buffer;
          DWORD bytes_rcvd;
          DWORD flags = 0;
          memset(wrapped_client_socket->read_info()->overlapped(), 0,
                 sizeof(OVERLAPPED));
          int status = WSARecv(
              wrapped_client_socket->socket(), &read_wsabuf, 1, &bytes_rcvd,
              &flags, wrapped_client_socket->read_info()->overlapped(), NULL);
          // Expecting error 997, WSA_IO_PENDING
          EXPECT_EQ(status, -1);
          int last_error = WSAGetLastError();
          ASSERT_EQ(last_error, WSA_IO_PENDING);
        }
        {
          // Have the server send a message to the client.
          WSABUF write_wsabuf;
          char write_char_buffer[20] = "hello!";
          write_wsabuf.len = 20;
          write_wsabuf.buf = write_char_buffer;
          DWORD bytes_sent;
          memset(wrapped_server_socket->write_info()->overlapped(), 0,
                 sizeof(OVERLAPPED));
          int status = WSASend(
              wrapped_server_socket->socket(), &write_wsabuf, 1, &bytes_sent, 0,
              wrapped_server_socket->write_info()->overlapped(), NULL);
          EXPECT_EQ(status, 0);
        }
      }
      iocp_worker.join();
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  absl::Time deadline = absl::Now() + absl::Seconds(30);
  while (read_count.load() != thread_count * sockets_per_thread ||
         write_count.load() != thread_count * sockets_per_thread) {
    absl::SleepFor(absl::Milliseconds(50));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded with " << read_count.load() << " reads and "
             << write_count.load() << " writes";
    }
  }
  ASSERT_EQ(read_count.load(), thread_count * sockets_per_thread);
  ASSERT_EQ(write_count.load(), thread_count * sockets_per_thread);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int status = RUN_ALL_TESTS();
  grpc_shutdown();
  return status;
}
#endif  // GPR_WINDOWS
