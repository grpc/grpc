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

#ifndef GRPC_TEST_CORE_TEST_UTIL_MOCK_TRANSPORT_FILTER_H
#define GRPC_TEST_CORE_TEST_UTIL_MOCK_TRANSPORT_FILTER_H

#include <stdint.h>

#include <optional>

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// A terminal v2 channel filter that stands in for the transport at the bottom
// of a grpc_channel_stack, for tests that drive a v2 call stack with
// grpc_transport_stream_op_batch batches.
//
// It does not send or receive anything. Instead, for every batch it:
//   - records the recv_* payload pointers and their ready closures in State,
//     so the test can fill in the payload and run the closure to simulate the
//     transport receiving data;
//   - completes on_complete immediately (unless held, see below);
//   - releases the call combiner, as connected_channel does.
//
// Usage: put `&MockTransportFilter::kFilter` last in the filter list, and add
// a MockTransportFilter::State to the channel args with
// `ChannelArgs::SetObject(&state)`. `state.call_combiner` must point at the
// call's call combiner.
class MockTransportFilter {
 public:
  struct State {
    struct RawPointerChannelArgTag {};
    static absl::string_view ChannelArgName() {
      return "grpc.test.mock_transport";
    }

    CallCombiner* call_combiner = nullptr;

    grpc_metadata_batch* recv_initial_metadata = nullptr;
    grpc_closure* recv_initial_metadata_ready = nullptr;

    std::optional<SliceBuffer>* recv_message = nullptr;
    uint32_t* recv_message_flags = nullptr;
    grpc_closure* recv_message_ready = nullptr;

    grpc_metadata_batch* recv_trailing_metadata = nullptr;
    grpc_closure* recv_trailing_metadata_ready = nullptr;
  };

  static const grpc_channel_filter kFilter;

 private:
  static void StartBatch(grpc_call_element* elem,
                         grpc_transport_stream_op_batch* op);
  static void StartTransportOp(grpc_channel_element* elem,
                               grpc_transport_op* op);
  static grpc_error_handle InitCallElem(grpc_call_element* elem,
                                        const grpc_call_element_args* args);
  static void DestroyCallElem(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* then_schedule_closure);
  static grpc_error_handle InitChannelElem(grpc_channel_element* elem,
                                           grpc_channel_element_args* args);
  static void DestroyChannelElem(grpc_channel_element* elem);
};

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TEST_UTIL_MOCK_TRANSPORT_FILTER_H
