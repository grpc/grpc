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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <memory>
#include <new>
#include <optional>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// A filter that, for the first call it sees, will fail all batches except
// for cancellations, so that the call fails with status ABORTED.
// All subsequent calls are allowed through without failures.
class FailFirstCallFilter {
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
      auto* chand = static_cast<FailFirstCallFilter*>(elem->channel_data);
      auto* calld = static_cast<CallData*>(elem->call_data);
      if (!chand->seen_first_) {
        chand->seen_first_ = true;
        calld->fail_ = true;
      }
      if (calld->fail_ && !batch->cancel_stream) {
        grpc_transport_stream_op_batch_finish_with_failure(
            batch,
            grpc_error_set_int(
                GRPC_ERROR_CREATE("FailFirstCallFilter failing batch"),
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
    bool fail_ = false;
  };

  static grpc_error_handle Init(grpc_channel_element* elem,
                                grpc_channel_element_args* /*args*/) {
    new (elem->channel_data) FailFirstCallFilter();
    return absl::OkStatus();
  }

  static void Destroy(grpc_channel_element* elem) {
    auto* chand = static_cast<FailFirstCallFilter*>(elem->channel_data);
    chand->~FailFirstCallFilter();
  }

  bool seen_first_ = false;
};

grpc_channel_filter FailFirstCallFilter::kFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(FailFirstCallFilter),
    Init,
    grpc_channel_stack_no_post_init,
    Destroy,
    grpc_channel_next_get_info,
    GRPC_UNIQUE_TYPE_NAME_HERE("FailFirstCallFilter"),
};

// Tests failure on a send op batch:
// - 2 retries allowed for ABORTED status
// - on the first call attempt, the batch containing the
//   send_initial_metadata op fails, and then the call returns ABORTED,
//   all without ever going out on the wire
// - second attempt returns ABORTED but does not retry, because only 2
//   attempts are allowed
CORE_END2END_TEST(RetryTests, RetrySendOpFails) {
  SKIP_IF_V3();  // Need to convert filter
  CoreConfiguration::RegisterEphemeralBuilder(
      [](CoreConfiguration::Builder* builder) {
        builder->channel_init()
            ->RegisterFilter(GRPC_CLIENT_SUBCHANNEL,
                             &FailFirstCallFilter::kFilterVtable)
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
  EXPECT_NE(c.GetPeer(), std::nullopt);
  // Start a batch containing send ops.
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .SendCloseFromClient();
  // Start a batch containing recv ops.
  IncomingMessage server_message;
  IncomingMetadata server_initial_metadata;
  IncomingStatusOnClient server_status;
  c.NewBatch(2)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message)
      .RecvStatusOnClient(server_status);
  // Client send ops should now complete.
  Expect(1, true);
  Step();
  // Server should get a call.
  auto s = RequestCall(101);
  Expect(101, true);
  Step();
  // Server fails with status ABORTED.
  IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_ABORTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  // In principle, the server batch should complete before the client
  // recv ops batch, but in the proxy fixtures, there are multiple threads
  // involved, so the completion order tends to be a little racy.
  Expect(102, true);
  Expect(2, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
  // Make sure the "grpc-previous-rpc-attempts" header was sent in the retry.
  EXPECT_EQ(s.GetInitialMetadata("grpc-previous-rpc-attempts"), "1");
}

}  // namespace
}  // namespace grpc_core
