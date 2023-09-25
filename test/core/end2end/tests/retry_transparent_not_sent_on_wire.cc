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

#include <string.h>

#include <new>

#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/impl/channel_arg_names.h>
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
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// A filter that, for the first 10 calls it sees, will fail all batches except
// for cancellations, so that the call fails with an error whose
// StreamNetworkState is kNotSentOnWire.
// All subsequent calls are allowed through without failures.
class FailFirstTenCallsFilter {
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
      auto* chand = static_cast<FailFirstTenCallsFilter*>(elem->channel_data);
      auto* calld = static_cast<CallData*>(elem->call_data);
      if (chand->num_calls_ < 10) calld->fail_ = true;
      if (batch->send_initial_metadata) ++chand->num_calls_;
      if (calld->fail_) {
        if (batch->recv_trailing_metadata) {
          batch->payload->recv_trailing_metadata.recv_trailing_metadata->Set(
              GrpcStreamNetworkState(), GrpcStreamNetworkState::kNotSentOnWire);
        }
        if (!batch->cancel_stream) {
          grpc_transport_stream_op_batch_finish_with_failure(
              batch,
              grpc_error_set_int(
                  GRPC_ERROR_CREATE("FailFirstTenCallsFilter failing batch"),
                  StatusIntProperty::kRpcStatus, GRPC_STATUS_UNAVAILABLE),
              calld->call_combiner_);
          return;
        }
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
    new (elem->channel_data) FailFirstTenCallsFilter();
    return absl::OkStatus();
  }

  static void Destroy(grpc_channel_element* elem) {
    auto* chand = static_cast<FailFirstTenCallsFilter*>(elem->channel_data);
    chand->~FailFirstTenCallsFilter();
  }

  size_t num_calls_ = 0;
};

grpc_channel_filter FailFirstTenCallsFilter::kFilterVtable = {
    CallData::StartTransportStreamOpBatch,
    nullptr,
    grpc_channel_next_op,
    sizeof(CallData),
    CallData::Init,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallData::Destroy,
    sizeof(FailFirstTenCallsFilter),
    Init,
    grpc_channel_stack_no_post_init,
    Destroy,
    grpc_channel_next_get_info,
    "FailFirstTenCallsFilter",
};

// Tests transparent retries when the call was never sent out on the wire.
CORE_END2END_TEST(RetryTest, RetryTransparentNotSentOnWire) {
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_init()->RegisterStage(
        GRPC_CLIENT_SUBCHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY + 1,
        [](ChannelStackBuilder* builder) {
          // Skip on proxy (which explicitly disables retries).
          if (!builder->channel_args()
                   .GetBool(GRPC_ARG_ENABLE_RETRIES)
                   .value_or(true)) {
            return true;
          }
          // Install filter.
          builder->PrependFilter(&FailFirstTenCallsFilter::kFilterVtable);
          return true;
        });
  });
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Minutes(1)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  // Start a batch containing send ops.
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .SendCloseFromClient();
  // Start a batch containing recv ops.
  IncomingStatusOnClient server_status;
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
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
  // Server receives the request.
  IncomingMessage client_message;
  s.NewBatch(102).RecvMessage(client_message);
  Expect(102, true);
  Step();
  // Server sends a response with status OK.
  IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendInitialMetadata({})
      .SendMessage("bar")
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  // In principle, the server batch should complete before the client
  // recv ops batch, but in the proxy fixtures, there are multiple threads
  // involved, so the completion order tends to be a little racy.
  Expect(103, true);
  Expect(2, true);
  Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(client_message.payload(), "foo");
  EXPECT_EQ(server_message.payload(), "bar");
  // Make sure the "grpc-previous-rpc-attempts" header was NOT sent, since
  // we don't do that for transparent retries.
  EXPECT_EQ(s.GetInitialMetadata("grpc-previous-rpc-attempts"), absl::nullopt);
}

}  // namespace
}  // namespace grpc_core
