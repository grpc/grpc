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

#include <memory>
#include <optional>

#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Tests that we can unref a call after the first attempt starts but
// before any ops complete.  This should not cause a memory leak.
CORE_END2END_TEST(RetryTests, RetryCancelAfterFirstAttemptStarts) {
  if (!IsRetryInCallv3Enabled()) SKIP_IF_V3();
  // This is a workaround for the flakiness that if the server ever enters
  // GracefulShutdown for whatever reason while the client has already been
  // shutdown, the test would not timeout and fail.
  InitServer(DefaultServerArgs());
  InitClient(ChannelArgs().Set(
      GRPC_ARG_SERVICE_CONFIG,
      "{\n"
      "  \"methodConfig\": [ {\n"
      "    \"name\": [\n"
      "      { \"service\": \"service\", \"method\": \"method\" }\n"
      "    ],\n"
      "    \"retryPolicy\": {\n"
      "      \"maxAttempts\": 3,\n"
      "      \"initialBackoff\": \"1s\",\n"
      "      \"maxBackoff\": \"120s\",\n"
      "      \"backoffMultiplier\": 1.6,\n"
      "      \"retryableStatusCodes\": [ \"ABORTED\" ]\n"
      "    }\n"
      "  } ]\n"
      "}"));
  std::optional<Call> c =
      NewClientCall("/service/method").Timeout(Duration::Seconds(6)).Create();
  // Client starts send ops.
  c->NewBatch(1)
      .SendInitialMetadata({})
      .SendMessage("foo")
      .SendCloseFromClient();
  // Client starts recv_initial_metadata and recv_message, but not
  // recv_trailing_metadata.
  IncomingMetadata server_initial_metadata;
  IncomingMessage server_message;
  c->NewBatch(2)
      .RecvInitialMetadata(server_initial_metadata)
      .RecvMessage(server_message);
  // Client starts recv_trailing_metadata.
  IncomingStatusOnClient server_status;
  c->NewBatch(3).RecvStatusOnClient(server_status);
  // Client unrefs the call without starting recv_trailing_metadata.
  // This should trigger a cancellation.
  c.reset();
  // The send ops batch and the first recv ops batch will fail in most
  // fixtures but will pass in the proxy fixtures on some platforms.
  Expect(1, AnyStatus());
  Expect(2, AnyStatus());
  Expect(3, true);
  Step();
}

}  // namespace
}  // namespace grpc_core
