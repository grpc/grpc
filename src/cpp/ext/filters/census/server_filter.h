//
//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_FILTERS_CENSUS_SERVER_FILTER_H
#define GRPC_SRC_CPP_EXT_FILTERS_CENSUS_SERVER_FILTER_H

#include <grpc/support/port_platform.h>

#include <stdint.h>
#include <string.h>

#include <string>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/opencensus.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/cpp/common/channel_filter.h"

namespace grpc {
namespace internal {

// A CallData class will be created for every grpc call within a channel. It is
// used to store data and methods specific to that call.
// OpenCensusServerCallData is thread-compatible, however typically only 1
// thread should be interacting with a call at a time.
class OpenCensusServerCallData : public CallData {
 public:
  // Maximum size of server stats that are sent on the wire.
  static constexpr uint32_t kMaxServerStatsLen = 16;

  OpenCensusServerCallData()
      : gc_(nullptr),
        auth_context_(nullptr),
        recv_initial_metadata_(nullptr),
        initial_on_done_recv_initial_metadata_(nullptr),
        initial_on_done_recv_message_(nullptr),
        recv_message_(nullptr),
        recv_message_count_(0),
        sent_message_count_(0) {
    memset(&on_done_recv_initial_metadata_, 0, sizeof(grpc_closure));
    memset(&on_done_recv_message_, 0, sizeof(grpc_closure));
  }

  grpc_error_handle Init(grpc_call_element* elem,
                         const grpc_call_element_args* args) override;

  void Destroy(grpc_call_element* elem, const grpc_call_final_info* final_info,
               grpc_closure* then_call_closure) override;

  void StartTransportStreamOpBatch(grpc_call_element* elem,
                                   TransportStreamOpBatch* op) override;

  static void OnDoneRecvInitialMetadataCb(void* user_data,
                                          grpc_error_handle error);

  static void OnDoneRecvMessageCb(void* user_data, grpc_error_handle error);

 private:
  experimental::CensusContext context_;
  // server method
  absl::string_view method_;
  std::string qualified_method_;
  grpc_core::Slice path_;
  // Pointer to the grpc_call element
  grpc_call* gc_;
  // Authorization context for the call.
  grpc_auth_context* auth_context_;
  // recv callback
  grpc_metadata_batch* recv_initial_metadata_;
  grpc_closure* initial_on_done_recv_initial_metadata_;
  grpc_closure on_done_recv_initial_metadata_;
  // recv message
  grpc_closure* initial_on_done_recv_message_;
  grpc_closure on_done_recv_message_;
  absl::Time start_time_;
  absl::Duration elapsed_time_;
  absl::optional<grpc_core::SliceBuffer>* recv_message_;
  uint64_t recv_message_count_;
  uint64_t sent_message_count_;
  // Buffer needed for grpc_slice to reference it when adding metatdata to
  // response.
  char stats_buf_[kMaxServerStatsLen];
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_FILTERS_CENSUS_SERVER_FILTER_H
