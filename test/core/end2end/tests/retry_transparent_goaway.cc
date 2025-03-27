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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>

#include <memory>
#include <new>
#include <optional>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/test_util/fail_first_call_filter.h"

namespace grpc_core {
namespace {

// Tests transparent retries when the call was never sent out on the wire.
CORE_END2END_TEST(RetryTests, TransparentGoaway) {
  SKIP_IF_V3();  // Need to convert filter
  SKIP_IF_CORE_CONFIGURATION_RESET_DISABLED();
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_init()
        ->RegisterFilter(GRPC_CLIENT_SUBCHANNEL,
                         &testing::FailFirstCallFilter::kFilterVtable)
        // Skip on proxy (which explicitly disables retries).
        .IfChannelArg(GRPC_ARG_ENABLE_RETRIES, true);
  });
  auto c =
      NewClientCall("/service/method").Timeout(Duration::Minutes(1)).Create();
  EXPECT_NE(c.GetPeer(), std::nullopt);
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
  EXPECT_EQ(server_status.message(), IsErrorFlattenEnabled() ? "" : "xyz");
  EXPECT_EQ(s.method(), "/service/method");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(server_message.payload(), "bar");
  EXPECT_EQ(client_message.payload(), "foo");
  // Make sure the "grpc-previous-rpc-attempts" header was NOT sent, since
  // we don't do that for transparent retries.
  EXPECT_EQ(s.GetInitialMetadata("grpc-previous-rpc-attempts"), std::nullopt);
}

}  // namespace
}  // namespace grpc_core
