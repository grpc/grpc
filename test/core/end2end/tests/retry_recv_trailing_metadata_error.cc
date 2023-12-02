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

#include <memory>
#include <new>

#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
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

// A filter that returns recv_trailing_metadata_ready with an error.
class InjectStatusFilter {
 public:
  static grpc_channel_filter kFilterVtable;

 private:
  class CallData {
   public:
    static grpc_error_handle Init(grpc_call_element* elem,
                                  const grpc_call_element_args* /*args*/) {
      new (elem->call_data) CallData();
      return absl::OkStatus();
    }

    static void Destroy(grpc_call_element* elem,
                        const grpc_call_final_info* /*final_info*/,
                        grpc_closure* /*ignored*/) {
      auto* calld = static_cast<CallData*>(elem->call_data);
      calld->~CallData();
    }

    static void StartTransportStreamOpBatch(
        grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
      auto* calld = static_cast<CallData*>(elem->call_data);
      if (batch->recv_trailing_metadata) {
        calld->original_recv_trailing_metadata_ready_ =
            batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
            &calld->recv_trailing_metadata_ready_;
      }
      grpc_call_next_op(elem, batch);
    }

   private:
    CallData() {
      GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                        RecvTrailingMetadataReady, this, nullptr);
    }

    static void RecvTrailingMetadataReady(void* arg,
                                          grpc_error_handle /*error*/) {
      auto* calld = static_cast<CallData*>(arg);
      Closure::Run(DEBUG_LOCATION,
                   calld->original_recv_trailing_metadata_ready_,
                   grpc_error_set_int(GRPC_ERROR_CREATE("injected error"),
                                      StatusIntProperty::kRpcStatus,
                                      GRPC_STATUS_INVALID_ARGUMENT));
    }

    grpc_closure recv_trailing_metadata_ready_;
    grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  };

  static grpc_error_handle Init(grpc_channel_element* /*elem*/,
                                grpc_channel_element_args* /*args*/) {
    return absl::OkStatus();
  }

  static void Destroy(grpc_channel_element* /*elem*/) {}
};

grpc_channel_filter InjectStatusFilter::kFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    nullptr,
    nullptr,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    0,
    Init,
    grpc_channel_stack_no_post_init,
    Destroy,
    grpc_channel_next_get_info,
    "InjectStatusFilter",
};

// Tests that we honor the error passed to recv_trailing_metadata_ready
// when determining the call's status, even if the op completion runs before
// the recv_trailing_metadata op is started from the surface.
// - 1 retry allowed for ABORTED status
// - server returns ABORTED, but filter overwrites to INVALID_ARGUMENT,
//   so no retry is done
CORE_END2END_TEST(RetryTest, RetryRecvTrailingMetadataError) {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_init()
        ->RegisterFilter(GRPC_CLIENT_SUBCHANNEL,
                         &InjectStatusFilter::kFilterVtable)
        // Skip on proxy (which explicitly disables retries).
        .IfChannelArg(GRPC_ARG_ENABLE_RETRIES, true);
  });
  InitServer(ChannelArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 2,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}"));
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(5)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingMessage server_message;
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .RecvMessage(server_message)
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata);
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  Expect(102, true);
  Expect(1, true);
  Step();
  IncomingStatusOnClient server_status;
  c.NewBatch(2).RecvStatusOnClient(server_status);
  Expect(2, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(server_status.message(), "injected error");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
