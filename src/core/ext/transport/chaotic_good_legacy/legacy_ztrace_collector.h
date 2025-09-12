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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_LEGACY_ZTRACE_COLLECTOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_LEGACY_ZTRACE_COLLECTOR_H

#include "src/core/channelz/property_list.h"
#include "src/core/channelz/ztrace_collector.h"
#include "src/core/ext/transport/chaotic_good_legacy/frame_header.h"

namespace grpc_core {
namespace chaotic_good_legacy {

namespace legacy_ztrace_collector_detail {

class Config {
 public:
  explicit Config(const channelz::ZTrace::Args&) {}

  template <typename T>
  bool Finishes(const T&) {
    return false;
  }
};

}  // namespace legacy_ztrace_collector_detail

struct ReadFrameTrace {
  FrameHeader header;

  channelz::PropertyList ChannelzProperties() const {
    return header.ChannelzProperties();
  }
};

struct WriteFrameTrace {
  FrameHeader header;

  channelz::PropertyList ChannelzProperties() const {
    return header.ChannelzProperties();
  }
};

struct ControlEndpointWriteTrace {
  size_t bytes;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("bytes", bytes);
  }
};

struct ControlEndpointQueueWriteTrace {
  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList();
  }
};

struct ControlEndpointReadRequestTrace {
  size_t bytes;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("bytes", bytes);
  }
};

struct ControlEndpointReadTrace {
  size_t bytes;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("bytes", bytes);
  }
};

struct DataEndpointQueueWriteTrace {
  size_t bytes;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("bytes", bytes);
  }
};

struct DataEndpointAcceptWriteTrace {
  size_t bytes;
  size_t connection_id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("connection_id", connection_id)
        .Set("bytes", bytes);
  }
};

struct DataEndpointWriteTrace {
  size_t bytes;
  size_t connection_id;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList()
        .Set("connection_id", connection_id)
        .Set("bytes", bytes);
  }
};

struct DataEndpointTicketReadPendingTrace {
  uint64_t ticket;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("ticket", ticket);
  }
};

struct DataEndpointTicketReadTrace {
  uint64_t ticket;
  absl::Status status;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("ticket", ticket).Set("status", status);
  }
};

struct DataEndpointCompleteReadTrace {
  uint64_t ticket;
  absl::StatusOr<size_t> bytes;

  channelz::PropertyList ChannelzProperties() const {
    return channelz::PropertyList().Set("ticket", ticket).Set("bytes", bytes);
  }
};

using LegacyZTraceCollector = channelz::ZTraceCollector<
    legacy_ztrace_collector_detail::Config, ReadFrameTrace, WriteFrameTrace,
    ControlEndpointWriteTrace, ControlEndpointQueueWriteTrace,
    ControlEndpointReadRequestTrace, ControlEndpointReadTrace,
    DataEndpointQueueWriteTrace, DataEndpointAcceptWriteTrace,
    DataEndpointWriteTrace, DataEndpointTicketReadPendingTrace,
    DataEndpointTicketReadTrace, DataEndpointCompleteReadTrace>;

}  // namespace chaotic_good_legacy
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_LEGACY_LEGACY_ZTRACE_COLLECTOR_H
