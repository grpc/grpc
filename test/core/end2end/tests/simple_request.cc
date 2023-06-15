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

#include <algorithm>
#include <memory>
#include <string>

#include "absl/strings/match.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

using testing::HasSubstr;
using testing::StartsWith;

namespace grpc_core {
namespace {
void CheckPeer(std::string peer_name) {
  // If the peer name is a uds path, then check if it is filled
  if (absl::StartsWith(peer_name, "unix:/")) {
    EXPECT_THAT(peer_name, StartsWith("unix:/tmp/grpc_fullstack_test."));
  }
}

void SimpleRequestBody(CoreEnd2endTest& test) {
  auto before = global_stats().Collect();
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Expect(
      1, CoreEnd2endTest::MaybePerformAction{[&](bool success) {
        Crash(absl::StrCat(
            "Unexpected completion of client side call: success=",
            success ? "true" : "false", " status=", server_status.ToString(),
            " initial_md=", server_initial_metadata.ToString()));
      }});
  test.Step();
  EXPECT_NE(s.GetPeer(), absl::nullopt);
  CheckPeer(*s.GetPeer());
  EXPECT_NE(c.GetPeer(), absl::nullopt);
  CheckPeer(*c.GetPeer());
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, "xyz", {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), "xyz");
  // the following sanity check makes sure that the requested error string is
  // correctly populated by the core. It looks for certain substrings that are
  // not likely to change much. Some parts of the error, like time created,
  // obviously are not checked.
  EXPECT_THAT(server_status.error_string(), HasSubstr("xyz"));
  EXPECT_THAT(server_status.error_string(),
              HasSubstr("Error received from peer"));
  EXPECT_THAT(server_status.error_string(), HasSubstr("grpc_message"));
  EXPECT_THAT(server_status.error_string(), HasSubstr("grpc_status"));
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  uint64_t expected_calls = 1;
  if (test.GetParam()->feature_mask & FEATURE_MASK_SUPPORTS_REQUEST_PROXYING) {
    expected_calls *= 2;
  }
  auto after = global_stats().Collect();
  gpr_log(GPR_DEBUG, "%s", StatsAsJson(after.get()).c_str());
  EXPECT_EQ(after->client_calls_created - before->client_calls_created,
            expected_calls);
  EXPECT_EQ(after->server_calls_created - before->server_calls_created,
            expected_calls);
}

CORE_END2END_TEST(CoreEnd2endTest, SimpleRequest) {
  SKIP_IF_USES_EVENT_ENGINE_LISTENER();
  SimpleRequestBody(*this);
}

CORE_END2END_TEST(CoreEnd2endTest, SimpleRequest10) {
  for (int i = 0; i < 10; i++) {
    SimpleRequestBody(*this);
  }
}
}  // namespace
}  // namespace grpc_core
