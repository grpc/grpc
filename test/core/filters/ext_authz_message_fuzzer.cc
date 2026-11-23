// Copyright 2026 gRPC authors.
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

#include <grpc/status.h>

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "envoy/service/auth/v3/external_auth.pb.h"
#include "fuzztest/fuzztest.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/filter/ext_authz/ext_authz_messages.h"
#include "src/core/lib/slice/slice.h"
#include "gtest/gtest.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::envoy::service::auth::v3::CheckRequest;
using ::envoy::service::auth::v3::CheckResponse;

// Checks the invariants of a successfully parsed CheckResponse: per gRFC
// A92, the status alone decides whether the RPC is allowed, and a denied RPC
// is never failed with OK.
void CheckParsedResponse(const ExtAuthzResponse& response) {
  if (response.status_code == GRPC_STATUS_OK) {
    EXPECT_TRUE(std::holds_alternative<ExtAuthzResponse::OkResponse>(
        response.response));
    return;
  }
  const auto* denied =
      std::get_if<ExtAuthzResponse::DeniedResponse>(&response.response);
  ASSERT_NE(denied, nullptr);
  EXPECT_NE(denied->status, GRPC_STATUS_OK);
}

// Parsing arbitrary bytes as a CheckResponse never crashes.
void ParseArbitraryBytes(std::string serialized) {
  auto response = ExtAuthzResponse::Parse(serialized);
  if (response.ok()) CheckParsedResponse(*response);
}
FUZZ_TEST(ExtAuthzMessageFuzzer, ParseArbitraryBytes);

// Parsing an arbitrary CheckResponse never crashes.
void ParseArbitraryCheckResponse(const CheckResponse& check_response) {
  auto response = ExtAuthzResponse::Parse(check_response.SerializeAsString());
  if (response.ok()) CheckParsedResponse(*response);
}
FUZZ_TEST(ExtAuthzMessageFuzzer, ParseArbitraryCheckResponse);

// A CheckRequest created from arbitrary request metadata is a valid
// CheckRequest, populated as described in gRFC A92.
void CreateExtAuthzRequestRoundTrips(
    bool is_client_call, std::string path,
    std::vector<std::pair<std::string, std::string>> metadata) {
  grpc_metadata_batch batch;
  for (const auto& [key, value] : metadata) {
    batch.Append(key, Slice::FromCopiedString(value),
                 [](absl::string_view, const Slice&) {});
  }
  ExtAuthzRequest params;
  params.is_client_call = is_client_call;
  params.path = path;
  params.metadata = &batch;
  auto serialized = CreateExtAuthzRequest(params);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  CheckRequest request;
  ASSERT_TRUE(request.ParseFromString(*serialized));
  const auto& http = request.attributes().request().http();
  EXPECT_EQ(http.method(), "POST");
  EXPECT_EQ(http.path(), path);
  EXPECT_EQ(http.size(), -1);
  EXPECT_EQ(http.protocol(), "HTTP/2");
  for (const auto& header : http.header_map().headers()) {
    // Per gRFC A102, raw_value is always set and value never is, and binary
    // header values are base64-encoded.
    EXPECT_TRUE(header.value().empty()) << header.key();
    if (absl::EndsWith(header.key(), "-bin")) {
      std::string decoded;
      EXPECT_TRUE(absl::Base64Unescape(header.raw_value(), &decoded))
          << header.key();
    }
  }
}
// The path and the header keys are proto string fields, so they are limited
// to valid header characters, which are valid UTF-8.  Header values are
// bytes fields, so they are arbitrary.
FUZZ_TEST(ExtAuthzMessageFuzzer, CreateExtAuthzRequestRoundTrips)
    .WithDomains(fuzztest::Arbitrary<bool>(),
                 fuzztest::StringOf(fuzztest::InRange<char>(0x20, 0x7e)),
                 fuzztest::VectorOf(fuzztest::PairOf(
                     fuzztest::StringOf(
                         fuzztest::OneOf(fuzztest::InRange<char>('a', 'z'),
                                         fuzztest::InRange<char>('0', '9'),
                                         fuzztest::ElementOf({'-', '_', '.'})))
                         .WithMinSize(1),
                     fuzztest::Arbitrary<std::string>())));

}  // namespace
}  // namespace grpc_core
