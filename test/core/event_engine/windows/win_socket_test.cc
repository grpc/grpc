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
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/time/time.h"

#include <grpc/grpc.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/event_engine/windows/create_sockpair.h"

namespace {
using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::CreateSockpair;
using ::grpc_event_engine::experimental::IOCP;
using ::grpc_event_engine::experimental::ThreadPool;
using ::grpc_event_engine::experimental::WinSocket;
}  // namespace

class WinSocketTest : public testing::Test {};

TEST_F(WinSocketTest, ManualReadEventTriggeredWithoutIO) {
  auto thread_pool = grpc_event_engine::experimental::MakeThreadPool(8);
  SOCKET sockpair[2];
  CreateSockpair(sockpair, IOCP::GetDefaultSocketFlags());
  WinSocket wrapped_client_socket(sockpair[0], thread_pool.get());
  WinSocket wrapped_server_socket(sockpair[1], thread_pool.get());
  bool read_called = false;
  AnyInvocableClosure on_read([&read_called]() { read_called = true; });
  wrapped_client_socket.NotifyOnRead(&on_read);
  AnyInvocableClosure on_write([] { FAIL() << "No Write expected"; });
  wrapped_client_socket.NotifyOnWrite(&on_write);
  ASSERT_FALSE(read_called);
  wrapped_client_socket.read_info()->SetReady();
  absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (!read_called) {
    absl::SleepFor(absl::Milliseconds(42));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded";
    }
  }
  ASSERT_TRUE(read_called);
  wrapped_client_socket.Shutdown();
  wrapped_server_socket.Shutdown();
  thread_pool->Quiesce();
}

TEST_F(WinSocketTest, NotificationCalledImmediatelyOnShutdownWinSocket) {
  auto thread_pool = grpc_event_engine::experimental::MakeThreadPool(8);
  SOCKET sockpair[2];
  CreateSockpair(sockpair, IOCP::GetDefaultSocketFlags());
  WinSocket wrapped_client_socket(sockpair[0], thread_pool.get());
  wrapped_client_socket.Shutdown();
  bool read_called = false;
  AnyInvocableClosure closure([&wrapped_client_socket, &read_called] {
    ASSERT_EQ(wrapped_client_socket.read_info()->result().bytes_transferred,
              0u);
    ASSERT_EQ(wrapped_client_socket.read_info()->result().wsa_error,
              WSAESHUTDOWN);
    read_called = true;
  });
  wrapped_client_socket.NotifyOnRead(&closure);
  absl::Time deadline = absl::Now() + absl::Seconds(3);
  while (!read_called) {
    absl::SleepFor(absl::Milliseconds(42));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded";
    }
  }
  ASSERT_TRUE(read_called);
  closesocket(sockpair[1]);
  thread_pool->Quiesce();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int status = RUN_ALL_TESTS();
  grpc_shutdown();
  return status;
}

#else  // not GPR_WINDOWS
int main(int /* argc */, char** /* argv */) { return 0; }
#endif
