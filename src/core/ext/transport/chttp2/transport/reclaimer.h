//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_RECLAIMER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_RECLAIMER_H

#include <memory>
#include <optional>
#include <utility>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_RECLAIMER_LOG VLOG(2)

// Each PH2 transport provides its own implementation of this interface.
// Each method maps to the actions that a memory reclamation pass performs on a
// PH2 transport.
class ReclaimerInterface {
 public:
  virtual ~ReclaimerInterface() = default;

  // Benign pass. Closes the transport if it has no active streams.
  // Returns true if the transport close was initiated.
  virtual bool CloseTransportIfIdle() = 0;

  // Destructive pass. Cancels exactly one active stream, if any exists.
  // Returns true if at least one stream is still active after the
  // cancellation. Returns false otherwise.
  virtual bool CancelOneActiveStream() = 0;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_RECLAIMER_H