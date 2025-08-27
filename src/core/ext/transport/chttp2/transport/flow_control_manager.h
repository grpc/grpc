//
//
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
//
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_MANAGER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_MANAGER_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"

namespace grpc_core {
namespace chttp2 {

class FlowControlManager {
 public:
  void IncrementStreamWindow(const uint32_t stream_id,
                             const uint32_t increment) {
    DCHECK_GT(increment, 0u);
    DCHECK_LE(increment, kMaxWindow - stream_window_updates_[stream_id]);
    stream_window_updates_[stream_id] += increment;
  }

  void IncrementTransportWindow(const uint32_t increment) {
    DCHECK_GT(increment, 0u);
    DCHECK_LE(increment, kMaxWindow - transport_window_update_size_);
    transport_window_update_size_ += increment;
  }

  std::vector<Http2Frame> GetWindowUpdates() {
    std::vector<Http2Frame> frames;
    if (transport_window_update_size_ > 0) {
      frames.push_back(Http2WindowUpdateFrame{
          /*stream_id=*/0, /*increment=*/transport_window_update_size_});
      transport_window_update_size_ = 0;
    }
    for (auto& pair : stream_window_updates_) {
      if (pair.second > 0) {
        frames.push_back(Http2WindowUpdateFrame{/*stream_id=*/pair.first,
                                                /*increment=*/pair.second});
      }
    }
    stream_window_updates_.clear();
    return frames;
  }

  bool HasWindowUpdates() const {
    return transport_window_update_size_ > 0 || !stream_window_updates_.empty();
  }

  void RemoveStream(const uint32_t stream_id) {
    stream_window_updates_.erase(stream_id);
  }

 private:
  uint32_t transport_window_update_size_ = 0;
  absl::flat_hash_map<uint32_t, uint32_t> stream_window_updates_;
};

}  // namespace chttp2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_MANAGER_H
