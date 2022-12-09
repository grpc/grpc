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

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/windows/windows_engine.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/event_engine/test_utils.h"

// This manual test requires you to run a listener externally.
// Example:
//
//    # in one shell
//    choco install nmap
//    ncat -klp 32000
//
//    # in a separate shell
//    bazel run //test/core/event_engine/windows:manual_client_test

namespace {
using namespace std::chrono_literals;

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::Slice;
using ::grpc_event_engine::experimental::SliceBuffer;
using ::grpc_event_engine::experimental::URIToResolvedAddress;

void SendMessage(EventEngine::Endpoint* endpoint, int message_id) {
  SliceBuffer buf;
  buf.Append(Slice::FromCopiedString(
      absl::StrFormat("Waiting for message %d ... \n", message_id)));
  grpc_core::Notification write_done;
  endpoint->Write(
      [&](absl::Status status) {
        GPR_ASSERT(status.ok());
        write_done.Notify();
      },
      &buf, nullptr);
  write_done.WaitForNotification();
}

void ReceiveAndEchoMessage(EventEngine::Endpoint* endpoint, int message_id) {
  SliceBuffer buf;
  grpc_core::Notification read_done;
  endpoint->Read(
      [&](absl::Status status) {
        GPR_ASSERT(status.ok());
        gpr_log(GPR_INFO, "Received message %d: %s", message_id,
                buf.TakeFirst().as_string_view().data());
        read_done.Notify();
      },
      &buf, nullptr);
  read_done.WaitForNotification();
}

void RunUntilInterrupted() {
  grpc_event_engine::experimental::WindowsEventEngine engine;
  std::unique_ptr<EventEngine::Endpoint> endpoint;
  grpc_core::Notification connected;
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  ChannelArgsEndpointConfig config;
  auto addr = URIToResolvedAddress("ipv6:[::1]:32000");
  auto handle = engine.Connect(
      [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep) {
        GPR_ASSERT(ep.ok());
        endpoint = std::move(*ep);
        connected.Notify();
      },
      addr, config, memory_quota->CreateMemoryAllocator("client"), 2h);
  connected.WaitForNotification();
  GPR_ASSERT(endpoint.get() != nullptr);
  gpr_log(GPR_DEBUG, "DO NOT SUBMIT: peer addr: %s",
          ResolvedAddressToString(endpoint->GetPeerAddress())->c_str());
  gpr_log(GPR_DEBUG, "DO NOT SUBMIT: local addr: %s",
          ResolvedAddressToString(endpoint->GetLocalAddress())->c_str());
  int message_id = 0;
  while (true) {
    SendMessage(endpoint.get(), message_id++);
    ReceiveAndEchoMessage(endpoint.get(), message_id);
  }
}
}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  RunUntilInterrupted();
  grpc_shutdown();
  return 0;
}

#else  // not GPR_WINDOWS

int main(int /* argc */, char** /* argv */) { return 0; }

#endif  // GPR_WINDOWS
