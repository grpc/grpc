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
#include <grpc/event_engine/slice.h>
#include <grpc/support/port_platform.h>
#include <stdlib.h>

#include "absl/log/check.h"

// The echo client wraps an EventEngine::Connect and EventEngine::Endpoint
// implementations, allowing third-party TCP listeners to interact with your
// EventEngine client. Example usage:
//
//    # in one shell
//    choco install nmap
//    ncat -klp 32000
//    # wait for a connection, than send data (e.g., keyboard input)
//
//    # in a separate shell
//    bazel run
//    //test/core/event_engine/test_suite/tools:my_event_engine_echo_client

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/util/notification.h"

extern absl::AnyInvocable<
    std::shared_ptr<grpc_event_engine::experimental::EventEngine>(void)>
CustomEventEngineFactory();

ABSL_FLAG(std::string, target, "ipv4:127.0.0.1:50051", "Target string");

namespace {
using namespace std::chrono_literals;

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
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
        CHECK_OK(status);
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
        if (!status.ok()) {
          LOG(ERROR) << "Error reading from endpoint: " << status;
          exit(1);
        }
        Slice received = buf.TakeFirst();
        LOG(ERROR) << "Received message " << message_id << ": "
                   << received.as_string_view();
        read_done.Notify();
      },
      &buf, nullptr);
  read_done.WaitForNotification();
}

void RunUntilInterrupted() {
  auto engine = GetDefaultEventEngine();
  std::unique_ptr<EventEngine::Endpoint> endpoint;
  grpc_core::Notification connected;
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  ChannelArgsEndpointConfig config{grpc_core::CoreConfiguration::Get()
                                       .channel_args_preconditioning()
                                       .PreconditionChannelArgs(nullptr)};
  std::string canonical_target =
      grpc_core::CoreConfiguration::Get()
          .resolver_registry()
          .AddDefaultPrefixIfNeeded(absl::GetFlag(FLAGS_target));
  auto addr = URIToResolvedAddress(canonical_target);
  CHECK_OK(addr);
  engine->Connect(
      [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep) {
        if (!ep.ok()) {
          LOG(ERROR) << "Error connecting: " << ep.status().ToString();
          exit(1);
        }
        endpoint = std::move(*ep);
        connected.Notify();
      },
      *addr, config, memory_quota->CreateMemoryAllocator("client"), 2h);
  connected.WaitForNotification();
  CHECK_NE(endpoint.get(), nullptr);
  VLOG(2) << "peer addr: "
          << ResolvedAddressToString(endpoint->GetPeerAddress());
  VLOG(2) << "local addr: "
          << ResolvedAddressToString(endpoint->GetLocalAddress());
  int message_id = 0;
  while (true) {
    SendMessage(endpoint.get(), message_id++);
    ReceiveAndEchoMessage(endpoint.get(), message_id);
  }
}
}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  grpc_event_engine::experimental::SetEventEngineFactory(
      CustomEventEngineFactory());
  grpc_init();
  RunUntilInterrupted();
  grpc_shutdown();
  return 0;
}
