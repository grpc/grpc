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

#ifndef GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_FRAME_TEST_HELPER_H
#define GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_FRAME_TEST_HELPER_H

#include <grpc/slice.h>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc_core {
namespace transport {
namespace testing {

using EventEngineSlice = grpc_event_engine::experimental::Slice;

class Http2FrameTestHelper {
 public:
  EventEngineSlice EventEngineSliceFromHttp2DataFrame(
      std::string_view data) const {
    return EventEngineSliceFromHttp2Frame(
        Http2DataFrame{1, false, SliceBufferFromString(data)});
  }

  // TODO(tjagtap) : [PH2][P1] : Add more helper functions as needed. One
  // function for each Http2Frame type that is being tested.

 private:
  EventEngineSlice EventEngineSliceFromHttp2Frame(Http2Frame frame) const {
    SliceBuffer buffer;
    Serialize(absl::Span<Http2Frame>(&frame, 1), buffer);
    return EventEngineSlice(buffer.JoinIntoSlice().TakeCSlice());
  }

  SliceBuffer SliceBufferFromString(absl::string_view s) const {
    SliceBuffer temp;
    temp.Append(Slice::FromCopiedString(s));
    return temp;
  }
};

}  // namespace testing
}  // namespace transport
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_CHTTP2_HTTP2_FRAME_TEST_HELPER_H
