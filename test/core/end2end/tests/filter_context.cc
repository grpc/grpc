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

#include <limits.h>

#include <algorithm>
#include <initializer_list>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

//******************************************************************************
// Test context filter
//

struct call_data {
  grpc_call_context_element* context;
};

grpc_error_handle init_call_elem(grpc_call_element* elem,
                                 const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->context = args->context;
  gpr_log(GPR_INFO, "init_call_elem(): context=%p", args->context);
  return absl::OkStatus();
}

void start_transport_stream_op_batch(grpc_call_element* elem,
                                     grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  // If batch payload context is not null (which will happen in some
  // cancellation cases), make sure we get the same context here that we
  // saw in init_call_elem().
  gpr_log(GPR_INFO, "start_transport_stream_op_batch(): context=%p",
          batch->payload->context);
  if (batch->payload->context != nullptr) {
    GPR_ASSERT(calld->context == batch->payload->context);
  }
  grpc_call_next_op(elem, batch);
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
    0,
    init_channel_elem,
    grpc_channel_stack_no_post_init,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "filter_context"};

// Simple request to test that filters see a consistent view of the
// call context.
CORE_END2END_TEST(CoreEnd2endTest, FilterContext) {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    for (auto type : {GRPC_CLIENT_CHANNEL, GRPC_CLIENT_SUBCHANNEL,
                      GRPC_CLIENT_DIRECT_CHANNEL, GRPC_SERVER_CHANNEL}) {
      builder->channel_init()->RegisterStage(
          type, INT_MAX, [](ChannelStackBuilder* builder) {
            // Want to add the filter as close to the end as possible, to
            // make sure that all of the filters work well together.
            // However, we can't add it at the very end, because the
            // connected channel filter must be the last one.  So we add it
            // right before the last one.
            auto it = builder->mutable_stack()->end();
            --it;
            builder->mutable_stack()->insert(it, &test_filter);
            return true;
          });
    }
  });
  auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("hello world")
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Expect(1, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
}

}  // namespace
}  // namespace grpc_core
