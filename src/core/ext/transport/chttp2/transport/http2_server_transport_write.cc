//
//
// Copyright 2024 gRPC authors.
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

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "src/core/call/call_destination.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_server_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace_impl.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/match_promise.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "absl/log/log.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_SERVER_DLOG \
  DLOG_IF(INFO, GRPC_TRACE_FLAG_ENABLED(http2_ph2_transport))

}  // namespace http2
}  // namespace grpc_core
