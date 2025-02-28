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
#include <grpc/grpc.h>
#include <grpc/support/log_windows.h>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/thread_pool/thread_pool.h"
#include "src/core/lib/event_engine/windows/iocp.h"
#include "src/core/lib/event_engine/windows/win_socket.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/notification.h"
#include "test/core/event_engine/windows/create_sockpair.h"

namespace {
using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::CreateSockpair;
using ::grpc_event_engine::experimental::IOCP;
using ::grpc_event_engine::experimental::ThreadPool;
using ::grpc_event_engine::experimental::WinSocket;
}  // namespace

class WinSocketTest : public testing::Test {
 public:
  WinSocketTest()
      : thread_pool_(grpc_event_engine::experimental::MakeThreadPool(8)) {
    CreateSockpair(sockpair_, IOCP::GetDefaultSocketFlags());
    wrapped_client_socket_ =
        std::make_unique<WinSocket>(sockpair_[0], thread_pool_.get());
    wrapped_server_socket_ =
        std::make_unique<WinSocket>(sockpair_[1], thread_pool_.get());
  }

  ~WinSocketTest() override {
    wrapped_client_socket_->Shutdown();
    wrapped_server_socket_->Shutdown();
    thread_pool_->Quiesce();
  }

 protected:
  std::shared_ptr<ThreadPool> thread_pool_;
  SOCKET sockpair_[2];
  std::unique_ptr<WinSocket> wrapped_client_socket_;
  std::unique_ptr<WinSocket> wrapped_server_socket_;
};

TEST_F(WinSocketTest, ManualReadEventTriggeredWithoutIO) {
  grpc_core::Notification read_called;
  AnyInvocableClosure on_read([&read_called]() { read_called.Notify(); });
  wrapped_client_socket_->NotifyOnRead(&on_read);
  AnyInvocableClosure on_write([] { FAIL() << "No Write expected"; });
  wrapped_client_socket_->NotifyOnWrite(&on_write);
  wrapped_client_socket_->read_info()->SetReady();
  read_called.WaitForNotification();
}

TEST_F(WinSocketTest, NotificationCalledImmediatelyOnShutdownWinSocket) {
  wrapped_client_socket_->Shutdown();
  grpc_core::Notification read_called;
  AnyInvocableClosure closure([this, &read_called] {
    ASSERT_EQ(wrapped_client_socket_->read_info()->result().bytes_transferred,
              0u);
    ASSERT_EQ(wrapped_client_socket_->read_info()->result().wsa_error,
              WSAESHUTDOWN);
    read_called.Notify();
  });
  wrapped_client_socket_->NotifyOnRead(&closure);
  read_called.WaitForNotification();
}

TEST_F(WinSocketTest, UnsetNotificationWorks) {
  AnyInvocableClosure read_closure{
      []() { grpc_core::Crash("read callback called"); }};
  wrapped_client_socket_->NotifyOnRead(&read_closure);
  AnyInvocableClosure write_closure{
      []() { grpc_core::Crash("write callback called"); }};
  wrapped_client_socket_->NotifyOnWrite(&write_closure);
  wrapped_client_socket_->UnregisterReadCallback();
  wrapped_client_socket_->UnregisterWriteCallback();
  // Give this time to fail.
  absl::SleepFor(absl::Seconds(1));
}

TEST_F(WinSocketTest, UnsetNotificationCanBeDoneRepeatedly) {
  // This should crash if a callback is already registered.
  AnyInvocableClosure closure{
      []() { grpc_core::Crash("read callback 1 called"); }};
  wrapped_client_socket_->NotifyOnRead(&closure);
  wrapped_client_socket_->UnregisterReadCallback();
  wrapped_client_socket_->NotifyOnRead(&closure);
  wrapped_client_socket_->UnregisterReadCallback();
  wrapped_client_socket_->NotifyOnRead(&closure);
  wrapped_client_socket_->UnregisterReadCallback();
  // Give this time to fail.
  absl::SleepFor(absl::Seconds(1));
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
