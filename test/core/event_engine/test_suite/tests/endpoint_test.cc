// Copyright 2025 gRPC authors.
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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/impl/channel_arg_names.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/notification.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/test_util/port.h"

class EventEngineEndpointTest : public EventEngineTest {};

namespace {

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::URIToResolvedAddress;
using Endpoint = ::grpc_event_engine::experimental::EventEngine::Endpoint;
using WriteArgs =
    ::grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs;
using WriteEvent =
    ::grpc_event_engine::experimental::EventEngine::Endpoint::WriteEvent;
using WriteMetric =
    ::grpc_event_engine::experimental::EventEngine::Endpoint::WriteMetric;
using WriteEventSink =
    ::grpc_event_engine::experimental::EventEngine::Endpoint::WriteEventSink;
using Listener = ::grpc_event_engine::experimental::EventEngine::Listener;
using ::grpc_event_engine::experimental::GetNextSendMessage;
using ::grpc_event_engine::experimental::SliceBuffer;

using namespace std::chrono_literals;

// Create a connection using the test EventEngine to a listener created by the
// test EventEngine and exchange bi-di data over the connection. Each endpoint
// gets reset as soon as the write is done. This test checks that EventEngine
// implementations handle lifetimes around endpoints correctly.

TEST_F(EventEngineEndpointTest, WriteEventCallbackEndpointValidityTest) {
  grpc_core::ExecCtx ctx;
  std::shared_ptr<EventEngine> test_ee(this->NewEventEngine());
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  CHECK_OK(resolved_addr);
  std::unique_ptr<EventEngine::Endpoint> client_endpoint;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint;
  std::unique_ptr<grpc_core::Notification> server_signal;

  Listener::AcceptCallback accept_cb =
      [&server_endpoint, &server_signal](
          std::unique_ptr<Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        server_endpoint = std::move(ep);
        server_signal->Notify();
      };

  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  ChannelArgsEndpointConfig config(args);
  auto listener = *test_ee->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) {
        ASSERT_TRUE(status.ok()) << status.ToString();
      },
      config, std::make_unique<grpc_core::MemoryQuota>("foo"));

  ASSERT_TRUE(listener->Bind(*resolved_addr).ok());
  ASSERT_TRUE(listener->Start().ok());

  constexpr int n_iterations = 100;
  for (int i = 0; i < n_iterations; ++i) {
    server_signal = std::make_unique<grpc_core::Notification>();
    grpc_core::Notification client_signal;
    test_ee->Connect(
        [&client_endpoint,
         &client_signal](absl::StatusOr<std::unique_ptr<Endpoint>> endpoint) {
          ASSERT_TRUE(endpoint.ok());
          client_endpoint = std::move(*endpoint);
          client_signal.Notify();
        },
        *resolved_addr, config, memory_quota->CreateMemoryAllocator("conn-1"),
        24h);

    client_signal.WaitForNotification();
    server_signal->WaitForNotification();
    ASSERT_NE(client_endpoint.get(), nullptr);
    ASSERT_NE(server_endpoint.get(), nullptr);

    // Start writes with WriteEventCallbacks from the client endpoint and server
    // endpoint and reset both endpoints immediately. It doesn't matter if the
    // callbacks don't get invoked as long as there is no use-after-free
    // behavior.
    auto event_cb = [](EventEngine::Endpoint* ee_ep, WriteEvent /*event*/,
                       absl::Time /*time*/,
                       std::vector<WriteMetric> /*metrics*/) {
      // some operation on the endpoint to ensure validity
      ASSERT_NE(ee_ep->GetPeerAddress().address(), nullptr);
    };
    SliceBuffer client_write_slice_buf;
    SliceBuffer server_write_slice_buf;
    WriteArgs client_write_args;
    client_write_args.set_metrics_sink(WriteEventSink(
        client_endpoint->AllWriteMetrics(),
        {WriteEvent::kSendMsg, WriteEvent::kScheduled, WriteEvent::kSent,
         WriteEvent::kAcked, WriteEvent::kClosed},
        event_cb));
    WriteArgs server_write_args;
    server_write_args.set_metrics_sink(WriteEventSink(
        client_endpoint->AllWriteMetrics(),
        {WriteEvent::kSendMsg, WriteEvent::kScheduled, WriteEvent::kSent,
         WriteEvent::kAcked, WriteEvent::kClosed},
        event_cb));
    AppendStringToSliceBuffer(&client_write_slice_buf, GetNextSendMessage());
    AppendStringToSliceBuffer(&server_write_slice_buf, GetNextSendMessage());
    client_endpoint->Write([&](absl::Status /*status*/) {},
                           &client_write_slice_buf,
                           std::move(client_write_args));
    server_endpoint->Write([&](absl::Status /*status*/) {},
                           &server_write_slice_buf,
                           std::move(server_write_args));
    client_endpoint.reset();
    server_endpoint.reset();
  }
  listener.reset();
}

}  // namespace
