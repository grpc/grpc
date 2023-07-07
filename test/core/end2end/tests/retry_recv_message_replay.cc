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

#include <new>

#include "absl/status/status.h"
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

namespace grpc_core {
namespace {

// A filter that, for the first call it sees, will fail the batch
// containing send_initial_metadata and then fail the call with status
// ABORTED.  All subsequent calls are allowed through without failures.
class FailFirstSendOpFilter {
 public:
  static grpc_channel_filter kFilterVtable;

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
      auto* chand = static_cast<FailFirstSendOpFilter*>(elem->channel_data);
      auto* calld = static_cast<CallData*>(elem->call_data);
      if (!chand->seen_first_) {
        chand->seen_first_ = true;
        calld->fail_ = true;
      }
      if (calld->fail_ && !batch->cancel_stream) {
        grpc_transport_stream_op_batch_finish_with_failure(
            batch,
            grpc_error_set_int(
                GRPC_ERROR_CREATE("FailFirstSendOpFilter failing batch"),
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
    new (elem->channel_data) FailFirstSendOpFilter();
    return absl::OkStatus();
  }

  static void Destroy(grpc_channel_element* elem) {
    auto* chand = static_cast<FailFirstSendOpFilter*>(elem->channel_data);
    chand->~FailFirstSendOpFilter();
  }

  bool seen_first_ = false;
};

grpc_channel_filter FailFirstSendOpFilter::kFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    nullptr,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(FailFirstSendOpFilter),
    Init,
    grpc_channel_stack_no_post_init,
    Destroy,
    grpc_channel_next_get_info,
    "FailFirstSendOpFilter",
};

// Tests the fix for a bug found in real-world code where recv_message
// was incorrectly replayed on a call attempt that it was already sent
// to when the recv_message completion had already been returned but was
// deferred at the point where recv_trailing_metadata was started from
// the surface.  This resulted in ASAN failures caused by not unreffing
// a grpc_error.
CORE_END2END_TEST(RetryTest, RetryRecvMessageReplay) {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_init()->RegisterStage(
        GRPC_CLIENT_SUBCHANNEL, 0, [](ChannelStackBuilder* builder) {
          // Skip on proxy (which explicitly disables retries).
          if (!builder->channel_args()
                   .GetBool(GRPC_ARG_ENABLE_RETRIES)
                   .value_or(true)) {
            return true;
          }
          // Install filter.
          builder->PrependFilter(&FailFirstSendOpFilter::kFilterVtable);
          return true;
        });
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
  // Start a batch containing send_initial_metadata and recv_initial_metadata.
  IncomingMetadata server_initial_metadata;
  c.NewBatch(1).SendInitialMetadata({}).RecvInitialMetadata(
      server_initial_metadata);
  // Start a batch containing recv_message.
  IncomingMessage server_message;
  c.NewBatch(2).RecvMessage(server_message);
  // Start a batch containing recv_trailing_metadata.
  IncomingStatusOnClient server_status;
  c.NewBatch(3).RecvStatusOnClient(server_status);
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
  // batches, but in the proxy fixtures, there are multiple threads
  // involved, so the completion order tends to be a little racy.
  Expect(102, true);
  Expect(1, true);
  Expect(2, true);
  Expect(3, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_ABORTED);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
}

}  // namespace
}  // namespace grpc_core
