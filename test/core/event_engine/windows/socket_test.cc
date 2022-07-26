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

#include "src/core/lib/event_engine/windows/socket.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/time/time.h"

#include <grpc/grpc.h>
#include <grpc/support/log_windows.h>

#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/event_engine/windows/basic_closure.h"
#include "test/core/event_engine/windows/create_sockpair.h"

namespace {
using ::grpc_event_engine::experimental::BasicClosure;
using ::grpc_event_engine::experimental::CreateSockpair;
using ::grpc_event_engine::experimental::IOCP;
using ::grpc_event_engine::experimental::WindowsEventEngine;
using ::grpc_event_engine::experimental::WinSocket;
}  // namespace

class SocketTest : public testing::Test {};

TEST_F(SocketTest, ManualReadEventTriggeredWithoutIO) {
  auto engine = absl::make_unique<WindowsEventEngine>();
  SOCKET sockpair[2];
  CreateSockpair(sockpair, IOCP::GetDefaultSocketFlags());
  WinSocket wrapped_client_socket(sockpair[0], engine.get());
  WinSocket wrapped_server_socket(sockpair[1], engine.get());
  bool read_called = false;
  BasicClosure on_read([&read_called]() { read_called = true; });
  wrapped_client_socket.NotifyOnRead(&on_read);
  BasicClosure on_write([] { FAIL() << "No Write expected"; });
  wrapped_client_socket.NotifyOnWrite(&on_write);
  ASSERT_FALSE(read_called);
  wrapped_client_socket.SetReadable();
  absl::Time deadline = absl::Now() + absl::Seconds(10);
  while (!read_called) {
    absl::SleepFor(absl::Milliseconds(42));
    if (deadline < absl::Now()) {
      FAIL() << "Deadline exceeded";
    }
  }
  ASSERT_TRUE(read_called);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int status = RUN_ALL_TESTS();
  grpc_shutdown();
  return status;
}
