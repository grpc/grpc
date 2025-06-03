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
#include "src/core/lib/event_engine/utils.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/internal/write_event.h>
#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/event_engine/extensions/blocking_dns.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/util/notification.h"
#include "src/core/util/time.h"

namespace grpc_event_engine::experimental {

std::string HandleToStringInternal(uintptr_t a, uintptr_t b) {
  return absl::StrCat("{", absl::Hex(a, absl::kZeroPad16), ",",
                      absl::Hex(b, absl::kZeroPad16), "}");
}

grpc_core::Timestamp ToTimestamp(grpc_core::Timestamp now,
                                 EventEngine::Duration delta) {
  return now +
         std::max(grpc_core::Duration::Milliseconds(1),
                  grpc_core::Duration::NanosecondsRoundUp(delta.count())) +
         grpc_core::Duration::Milliseconds(1);
}

absl::StatusOr<std::vector<EventEngine::ResolvedAddress>>
LookupHostnameBlocking(EventEngine::DNSResolver* dns_resolver,
                       absl::string_view name, absl::string_view default_port) {
  auto* blocking_resolver =
      QueryExtension<ResolverSupportsBlockingLookups>(dns_resolver);
  if (blocking_resolver != nullptr) {
    return blocking_resolver->LookupHostnameBlocking(name, default_port);
  }

  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> results;
  grpc_core::Notification done;
  dns_resolver->LookupHostname(
      [&](absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> addresses) {
        results = std::move(addresses);
        done.Notify();
      },
      name, default_port);
  done.WaitForNotification();
  return results;
}

absl::string_view WriteEventToString(
    grpc_event_engine::experimental::internal::WriteEvent event) {
  switch (event) {
    case grpc_event_engine::experimental::internal::WriteEvent::kSendMsg:
      return "SENDMSG";
    case grpc_event_engine::experimental::internal::WriteEvent::kScheduled:
      return "SCHEDULED";
    case grpc_event_engine::experimental::internal::WriteEvent::kSent:
      return "SENT";
    case grpc_event_engine::experimental::internal::WriteEvent::kAcked:
      return "ACKED";
    case grpc_event_engine::experimental::internal::WriteEvent::kClosed:
      return "CLOSED";
    case grpc_event_engine::experimental::internal::WriteEvent::kCount:
      break;
  }
  LOG(FATAL) << "WriteEventToString called with " << static_cast<int>(event);
  return "UNKNOWN";
}

}  // namespace grpc_event_engine::experimental
