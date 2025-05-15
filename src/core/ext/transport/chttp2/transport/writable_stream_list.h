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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITABLE_STREAM_LIST_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITABLE_STREAM_LIST_H

#include <cstdint>
#include <utility>

#include "src/core/call/call_spine.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/ext/transport/chttp2/transport/http2_settings.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/http2_transport.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/lib/promise/inter_activity_mutex.h"
#include "src/core/lib/promise/mpsc.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

namespace grpc_core {
namespace http2 {

class WritableStreamList {
 public:
  ~WritableStreamList() = default;

  WritableStreamList(WritableStreamList&& rhs) = delete;
  WritableStreamList& operator=(WritableStreamList&& rhs) = delete;
  WritableStreamList(const WritableStreamList&) = delete;
  WritableStreamList& operator=(const WritableStreamList&) = delete;

 private:
  // One data str to hold list of streams
  // For a stream to be writable all the following conditions need to be true
  // 1. Enough transport level flow control
  // 2. Enough stream level flow control
  // 3. Data to be written

  // *Maybe* One list to hold list of RST_STREAMS
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_WRITABLE_STREAM_LIST_H
