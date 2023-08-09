//
//
// Copyright 2015 gRPC authors.
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

#include <stdint.h>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/status.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

//******************************************************************************
// Test filter - always closes incoming requests
//

typedef struct {
  grpc_closure* recv_im_ready;
} call_data;

typedef struct {
  uint8_t unused;
} channel_data;

void recv_im_ready(void* arg, grpc_error_handle error) {
  grpc_call_element* elem = static_cast<grpc_call_element*>(arg);
  call_data* calld = static_cast<call_data*>(elem->call_data);
  Closure::Run(
      DEBUG_LOCATION, calld->recv_im_ready,
      grpc_error_set_int(GRPC_ERROR_CREATE_REFERENCING(
                             "Failure that's not preventable.", &error, 1),
                         StatusIntProperty::kRpcStatus,
                         GRPC_STATUS_PERMISSION_DENIED));
}

void start_transport_stream_op_batch(grpc_call_element* elem,
                                     grpc_transport_stream_op_batch* op) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (op->recv_initial_metadata) {
    calld->recv_im_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        GRPC_CLOSURE_CREATE(recv_im_ready, elem, grpc_schedule_on_exec_ctx);
  }
  grpc_call_next_op(elem, op);
}

grpc_error_handle init_call_elem(grpc_call_element* /*elem*/,
                                 const grpc_call_element_args* /*args*/) {
  return absl::OkStatus();
}

void destroy_call_elem(grpc_call_element* /*elem*/,
                       const grpc_call_final_info* /*final_info*/,
                       grpc_closure* /*ignored*/) {}

grpc_error_handle init_channel_elem(grpc_channel_element* /*elem*/,
                                    grpc_channel_element_args* /*args*/) {
  return absl::OkStatus();
}

void destroy_channel_elem(grpc_channel_element* /*elem*/) {}

const grpc_channel_filter test_filter = {
    start_transport_stream_op_batch,
    nullptr,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    grpc_channel_stack_no_post_init,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "filter_causes_close"};

CORE_END2END_TEST(CoreEnd2endTest, FilterCausesClose) {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_init()->RegisterStage(
        GRPC_SERVER_CHANNEL, 0, [](ChannelStackBuilder* builder) {
          builder->PrependFilter(&test_filter);
          return true;
        });
  });
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  Expect(1, true);
  Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_PERMISSION_DENIED);
  EXPECT_EQ(server_status.message(), "Failure that's not preventable.");
}

}  // namespace
}  // namespace grpc_core
