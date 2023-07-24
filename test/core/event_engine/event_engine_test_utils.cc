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

#include "test/core/event_engine/event_engine_test_utils.h"

#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <ratio>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"

// IWYU pragma: no_include <sys/socket.h>

namespace grpc_event_engine {
namespace experimental {

namespace {
constexpr size_t kMinMessageSize = 1024;
constexpr size_t kMaxMessageSize = 4096;

using namespace std::chrono_literals;

}  // namespace

// Returns a random message with bounded length.
std::string GetNextSendMessage() {
  return GetRandomBoundedMessage(kMinMessageSize, kMaxMessageSize);
}

std::string GetRandomBoundedMessage(size_t min_length, size_t max_length) {
  static std::random_device rd;
  static std::seed_seq seed{rd()};
  static std::mt19937 gen(seed);
  static std::uniform_real_distribution<> dis(min_length, max_length);
  static grpc_core::Mutex g_mu;
  int len;
  {
    grpc_core::MutexLock lock(&g_mu);
    len = dis(gen);
  }
  return GetRandomMessage(len);
}

std::string GetRandomMessage(size_t message_length) {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  std::string tmp_s;
  tmp_s.reserve(message_length);
  for (size_t i = 0; i < message_length; ++i) {
    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  }
  return tmp_s;
}

void WaitForSingleOwner(std::shared_ptr<EventEngine> engine) {
  while (engine.use_count() > 1) {
    GRPC_LOG_EVERY_N_SEC(2, GPR_INFO, "engine.use_count() = %ld",
                         engine.use_count());
    absl::SleepFor(absl::Milliseconds(100));
  }
}

void AppendStringToSliceBuffer(SliceBuffer* buf, absl::string_view data) {
  buf->Append(Slice::FromCopiedString(data));
}

std::string ExtractSliceBufferIntoString(SliceBuffer* buf) {
  if (!buf->Length()) {
    return std::string();
  }
  std::string tmp(buf->Length(), '\0');
  char* bytes = const_cast<char*>(tmp.c_str());
  grpc_slice_buffer_move_first_into_buffer(buf->c_slice_buffer(), buf->Length(),
                                           bytes);
  return tmp;
}

absl::Status SendValidatePayload(absl::string_view data,
                                 EventEngine::Endpoint* send_endpoint,
                                 EventEngine::Endpoint* receive_endpoint) {
  GPR_ASSERT(receive_endpoint != nullptr && send_endpoint != nullptr);
  int num_bytes_written = data.size();
  grpc_core::Notification read_signal;
  grpc_core::Notification write_signal;
  SliceBuffer read_slice_buf;
  SliceBuffer read_store_buf;
  SliceBuffer write_slice_buf;

  read_slice_buf.Clear();
  write_slice_buf.Clear();
  read_store_buf.Clear();
  // std::cout << "SendValidatePayload ... " << std::endl;
  // fflush(stdout);

  AppendStringToSliceBuffer(&write_slice_buf, data);
  EventEngine::Endpoint::ReadArgs args = {num_bytes_written};
  std::function<void(absl::Status)> read_cb;
  read_cb = [receive_endpoint, &read_slice_buf, &read_store_buf, &read_cb,
             &read_signal, &args](absl::Status status) {
    GPR_ASSERT(status.ok());
    if (read_slice_buf.Length() == static_cast<size_t>(args.read_hint_bytes)) {
      read_slice_buf.MoveFirstNBytesIntoSliceBuffer(read_slice_buf.Length(),
                                                    read_store_buf);
      read_signal.Notify();
      return;
    }
    args.read_hint_bytes -= read_slice_buf.Length();
    read_slice_buf.MoveFirstNBytesIntoSliceBuffer(read_slice_buf.Length(),
                                                  read_store_buf);
    if (receive_endpoint->Read(read_cb, &read_slice_buf, &args)) {
      GPR_ASSERT(read_slice_buf.Length() != 0);
      read_cb(absl::OkStatus());
    }
  };
  // Start asynchronous reading at the receive_endpoint.
  if (receive_endpoint->Read(read_cb, &read_slice_buf, &args)) {
    read_cb(absl::OkStatus());
  }
  // Start asynchronous writing at the send_endpoint.
  if (send_endpoint->Write(
          [&write_signal](absl::Status status) {
            GPR_ASSERT(status.ok());
            write_signal.Notify();
          },
          &write_slice_buf, nullptr)) {
    write_signal.Notify();
  }
  write_signal.WaitForNotification();
  read_signal.WaitForNotification();
  // Check if data written == data read
  std::string data_read = ExtractSliceBufferIntoString(&read_store_buf);
  if (data != data_read) {
    gpr_log(GPR_INFO, "Data written = %s", data.data());
    gpr_log(GPR_INFO, "Data read = %s", data_read.c_str());
    return absl::CancelledError("Data read != Data written");
  }
  return absl::OkStatus();
}

absl::StatusOr<SimpleConnectionFactory::Endpoints>
SimpleConnectionFactory::Connect(EventEngine* client_engine,
                                 EventEngine* listener_engine,
                                 absl::string_view target_addr) {
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("foo");
  auto resolved_addr = URIToResolvedAddress(std::string(target_addr));
  GRPC_RETURN_IF_ERROR(resolved_addr.status());
  absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> client_endpoint;
  std::unique_ptr<EventEngine::Endpoint> listener_endpoint;
  grpc_core::Notification client_signal;
  grpc_core::Notification listener_signal;
  // Create and bind a listener
  EventEngine::Listener::AcceptCallback accept_cb =
      [&listener_endpoint, &listener_signal](
          std::unique_ptr<EventEngine::Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        listener_endpoint = std::move(ep);
        listener_signal.Notify();
      };
  grpc_core::ChannelArgs args;
  auto quota = grpc_core::ResourceQuota::Default();
  args = args.Set(GRPC_ARG_RESOURCE_QUOTA, quota);
  ChannelArgsEndpointConfig config(args);
  auto listener = listener_engine->CreateListener(
      std::move(accept_cb),
      [](absl::Status status) { GPR_ASSERT(status.ok()); }, config,
      std::make_unique<grpc_core::MemoryQuota>("foo"));
  GRPC_RETURN_IF_ERROR(listener.status());
  GRPC_RETURN_IF_ERROR((*listener)->Bind(*resolved_addr).status());
  GRPC_RETURN_IF_ERROR((*listener)->Start());
  // Connect a client from the EventEngine under test
  client_engine->Connect(
      [&client_endpoint, &client_signal](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
        client_endpoint = std::move(endpoint);
        client_signal.Notify();
      },
      *resolved_addr, config,
      memory_quota->CreateMemoryAllocator("simple_conn"), 1h);
  // Wait for the connection to become established
  client_signal.WaitForNotification();
  listener_signal.WaitForNotification();
  GRPC_RETURN_IF_ERROR(client_endpoint.status());
  GPR_ASSERT(client_endpoint->get() != nullptr);
  GPR_ASSERT(listener_endpoint.get() != nullptr);
  return Endpoints{/*client=*/std::move(*client_endpoint),
                   /*listener=*/std::move(listener_endpoint)};
}

}  // namespace experimental
}  // namespace grpc_event_engine
