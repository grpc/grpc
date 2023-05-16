//
//
// Copyright 2022 gRPC authors.
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

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include <grpc/status.h>

#include "src/core/lib/gprpp/time.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

// Request/response with metadata which should be filtered
void TestRequestResponseWithMetadataToBeFiltered(
    CoreEnd2endTest& test, absl::string_view filtered_md_key,
    absl::string_view filter_md_value) {
  auto c = test.NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();

  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  c.NewBatch(1)
      .SendInitialMetadata(
          {{"key1", "val1"}, {filtered_md_key, filter_md_value}})
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);

  auto s = test.RequestCall(101);

  test.Expect(101, true);
  test.Step();

  s.NewBatch(102).SendInitialMetadata(
      {{"key2", "val2"}, {filtered_md_key, filter_md_value}});
  test.Expect(102, true);
  test.Step();

  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(103)
      .RecvCloseOnServer(client_close)
      .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
  test.Expect(103, true);
  test.Expect(1, true);
  test.Step();

  EXPECT_EQ(server_status.status(), GRPC_STATUS_OK);
  EXPECT_EQ(server_status.message(), "xyz");
  EXPECT_EQ(s.method(), "/foo");
  EXPECT_FALSE(client_close.was_cancelled());
  EXPECT_EQ(s.GetInitialMetadata("key1"), "val1");
  EXPECT_EQ(s.GetInitialMetadata(filtered_md_key), absl::nullopt);
  EXPECT_EQ(server_initial_metadata.Get("key2"), "val2");
  EXPECT_EQ(server_initial_metadata.Get(filtered_md_key), absl::nullopt);
}

CORE_END2END_TEST(CoreEnd2endTest, ContentLengthIsFiltered) {
  SKIP_IF_USES_EVENT_ENGINE_CLIENT();
  TestRequestResponseWithMetadataToBeFiltered(*this, "content-length", "45");
}

}  // namespace
}  // namespace grpc_core
