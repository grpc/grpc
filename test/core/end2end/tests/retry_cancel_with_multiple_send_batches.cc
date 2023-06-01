//
//
// Copyright 2017 gRPC authors.
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

#include <initializer_list>
#include <memory>
#include <new>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace {

// Tests cancellation with multiple send op batches.
void TestRetryCancelWithMultipleSendBatches(
    CoreEnd2endTest& test, std::unique_ptr<CancellationMode> mode) {
  test.InitServer(ChannelArgs());
  test.InitClient(
      ChannelArgs()
          .Set(
              GRPC_ARG_SERVICE_CONFIG,
              absl::StrFormat(
                  "{\n"
                  "  \"methodConfig\": [ {\n"
                  "    \"name\": [\n"
                  "      { \"service\": \"service\", \"method\": \"method\" }\n"
                  "    ],\n"
                  "    \"retryPolicy\": {\n"
                  "      \"maxAttempts\": 2,\n"
                  "      \"initialBackoff\": \"%ds\",\n"
                  "      \"maxBackoff\": \"120s\",\n"
                  "      \"backoffMultiplier\": 1.6,\n"
                  "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
                  "    }\n"
                  "  } ]\n"
                  "}",
                  5 * grpc_test_slowdown_factor()))
          // TODO(roth): do we need this now?
          .Set(GRPC_ARG_ENABLE_RETRIES, true));
  auto c = test.NewClientCall("/service/method")
               .Timeout(Duration::Seconds(3))
               .Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Start a batch containing send_initial_metadata.
  c.NewBatch(1).SendInitialMetadata({});
  // Start a batch containing send_message.
  c.NewBatch(2).SendMessage("foo");
  // Start a batch containing send_trailing_metadata.
  c.NewBatch(3).SendCloseFromClient();
  // Start a batch containing recv ops.
  CoreEnd2endTest::IncomingMessage server_message;
  CoreEnd2endTest::IncomingMetadata server_incoming_metadata;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(4)
      .RecvInitialMetadata(server_incoming_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  // Initiate cancellation.
  mode->Apply(c);
  // Client ops should now complete.
  test.Expect(1, false);
  test.Expect(2, false);
  test.Expect(3, false);
  test.Expect(4, true);
  test.Step();
  EXPECT_EQ(server_status.status(), mode->ExpectedStatus());
}

// A filter that fails all batches with send ops.
class FailSendOpsFilter {
 public:
  static grpc_channel_filter kFilterVtable;

 private:
  class CallData {
   public:
    static grpc_error_handle Init(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
      new (elem->call_data) CallData(args);
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
      if (batch->send_initial_metadata || batch->send_message ||
          batch->send_trailing_metadata) {
        grpc_transport_stream_op_batch_finish_with_failure(
            batch,
            grpc_error_set_int(
                GRPC_ERROR_CREATE("FailSendOpsFilter failing batch"),
                StatusIntProperty::kRpcStatus, GRPC_STATUS_ABORTED),
            calld->call_combiner_);
        return;
      }
      grpc_call_next_op(elem, batch);
    }

   private:
    explicit CallData(const grpc_call_element_args* args)
        : call_combiner_(args->call_combiner) {}

    CallCombiner* call_combiner_;
  };

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* /*args*/) {
    new (elem->channel_data) FailSendOpsFilter();
    return absl::OkStatus();
  }

  static void Destroy(grpc_channel_element* elem) {
    auto* chand = static_cast<FailSendOpsFilter*>(elem->channel_data);
    chand->~FailSendOpsFilter();
  }
};

grpc_channel_filter FailSendOpsFilter::kFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    nullptr,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(FailSendOpsFilter),
    Init,
    grpc_channel_stack_no_post_init,
    Destroy,
    grpc_channel_next_get_info,
    "FailSendOpsFilter",
};

bool MaybeAddFilter(ChannelStackBuilder* builder) {
  // Skip on proxy (which explicitly disables retries).
  if (!builder->channel_args()
           .GetBool(GRPC_ARG_ENABLE_RETRIES)
           .value_or(true)) {
    return true;
  }
  // Install filter.
  builder->PrependFilter(&FailSendOpsFilter::kFilterVtable);
  return true;
}

void RegisterFilter() {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_init()->RegisterStage(GRPC_CLIENT_SUBCHANNEL, 0,
                                           MaybeAddFilter);
  });
}

CORE_END2END_TEST(RetryTest, RetryCancelWithMultipleSendBatches) {
  RegisterFilter();
  TestRetryCancelWithMultipleSendBatches(
      *this, std::make_unique<CancelCancellationMode>());
}

CORE_END2END_TEST(RetryTest, RetryDeadlineWithMultipleSendBatches) {
  RegisterFilter();
  TestRetryCancelWithMultipleSendBatches(
      *this, std::make_unique<DeadlineCancellationMode>());
}

}  // namespace
}  // namespace grpc_core
