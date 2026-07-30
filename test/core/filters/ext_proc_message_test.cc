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
//

#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "src/core/filter/ext_proc/ext_proc_messages.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

constexpr absl::string_view kKey1 = "key1";
constexpr absl::string_view kVal1 = "val1";
constexpr absl::string_view kKey2 = "key2";
constexpr absl::string_view kVal2 = "val2";
constexpr absl::string_view kKey3 = "key3";
constexpr absl::string_view kVal3 = "val3";

MATCHER_P3(IsHeaderValueOption, key, value, append_action, "") {
  return ::testing::ExplainMatchResult(::testing::Pair(key, value), arg.header,
                                       result_listener) &&
         ::testing::ExplainMatchResult(append_action, arg.append_action,
                                       result_listener);
}

MATCHER_P2(IsHeader, key, value, "") {
  return ::testing::ExplainMatchResult(key, arg.key(), result_listener) &&
         ::testing::ExplainMatchResult(value, arg.raw_value(), result_listener);
}

MATCHER_P2(IsHeaderMutation, set_headers_matcher, remove_headers_matcher, "") {
  return ::testing::ExplainMatchResult(set_headers_matcher, arg.set_headers,
                                       result_listener) &&
         ::testing::ExplainMatchResult(remove_headers_matcher,
                                       arg.remove_headers, result_listener);
}

MATCHER_P3(IsBodyMutation, body_matcher, end_of_stream_matcher,
           end_of_stream_without_message_matcher, "") {
  return ::testing::ExplainMatchResult(body_matcher, arg.body,
                                       result_listener) &&
         ::testing::ExplainMatchResult(end_of_stream_matcher, arg.end_of_stream,
                                       result_listener) &&
         ::testing::ExplainMatchResult(end_of_stream_without_message_matcher,
                                       arg.end_of_stream_without_message,
                                       result_listener);
}

MATCHER_P2(IsRequestHeaders, set_headers_matcher, remove_headers_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtProcResponse::RequestHeaders>(::testing::Field(
          &ExtProcResponse::RequestHeaders::mutation,
          IsHeaderMutation(set_headers_matcher, remove_headers_matcher))),
      arg, result_listener);
}

MATCHER_P2(IsResponseHeaders, set_headers_matcher, remove_headers_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtProcResponse::ResponseHeaders>(::testing::Field(
          &ExtProcResponse::ResponseHeaders::mutation,
          IsHeaderMutation(set_headers_matcher, remove_headers_matcher))),
      arg, result_listener);
}

MATCHER_P2(IsResponseTrailers, set_headers_matcher, remove_headers_matcher,
           "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtProcResponse::ResponseTrailers>(
          ::testing::Field(
              &ExtProcResponse::ResponseTrailers::mutation,
              IsHeaderMutation(set_headers_matcher, remove_headers_matcher))),
      arg, result_listener);
}

MATCHER_P3(IsRequestBody, body_matcher, end_of_stream_matcher,
           end_of_stream_without_message_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtProcResponse::RequestBody>(::testing::Field(
          &ExtProcResponse::RequestBody::mutation,
          IsBodyMutation(body_matcher, end_of_stream_matcher,
                         end_of_stream_without_message_matcher))),
      arg, result_listener);
}

MATCHER_P3(IsResponseBody, body_matcher, end_of_stream_matcher,
           end_of_stream_without_message_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtProcResponse::ResponseBody>(::testing::Field(
          &ExtProcResponse::ResponseBody::mutation,
          IsBodyMutation(body_matcher, end_of_stream_matcher,
                         end_of_stream_without_message_matcher))),
      arg, result_listener);
}

MATCHER_P3(IsImmediateResponse, status_matcher, details_matcher,
           header_mutation_matcher, "") {
  return ::testing::ExplainMatchResult(
      ::testing::VariantWith<ExtProcResponse::ImmediateResponse>(
          ::testing::AllOf(
              ::testing::Field(&ExtProcResponse::ImmediateResponse::status,
                               status_matcher),
              ::testing::Field(&ExtProcResponse::ImmediateResponse::details,
                               details_matcher),
              ::testing::Field(
                  &ExtProcResponse::ImmediateResponse::header_mutation,
                  header_mutation_matcher))),
      arg, result_listener);
}

//
// Create*Request() tests
//

class CreateExtProcRequestTest : public ::testing::Test {
 protected:
  envoy::service::ext_proc::v3::ProcessingRequest ParseRequest(
      const std::string& serialized) {
    envoy::service::ext_proc::v3::ProcessingRequest parsed;
    EXPECT_TRUE(parsed.ParseFromString(serialized));
    return parsed;
  }
};

TEST_F(CreateExtProcRequestTest, RequestHeadersNeitherAllowedNorDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::string serialized =
      CreateExtProcClientHeadersRequest(arena.ptr(), &batch, {}, {}, {},
                                        /*observability_mode=*/false,
                                        /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  EXPECT_THAT(
      parsed.request_headers().headers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey2, kVal2),
                             IsHeader(kKey3, kVal3)));
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, RequestHeadersBothAllowedAndDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1).value(),
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::string serialized = CreateExtProcClientHeadersRequest(
                               arena.ptr(), &batch, allowed, disallowed, {},
                               /*observability_mode=*/false,
                               /*processing_mode=*/std::nullopt)
                               .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  EXPECT_THAT(parsed.request_headers().headers().headers(),
              ::testing::ElementsAre(IsHeader(kKey1, kVal1)));
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, RequestHeadersOnlyAllowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1).value(),
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::string serialized =
      CreateExtProcClientHeadersRequest(arena.ptr(), &batch, allowed, {}, {},
                                        /*observability_mode=*/false,
                                        /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  EXPECT_THAT(
      parsed.request_headers().headers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey3, kVal3)));
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, RequestHeadersOnlyDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey2).value(),
  };
  std::string serialized =
      CreateExtProcClientHeadersRequest(arena.ptr(), &batch, {}, disallowed, {},
                                        /*observability_mode=*/false,
                                        /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  EXPECT_THAT(
      parsed.request_headers().headers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey3, kVal3)));
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, RequestHeadersObservability) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized =
      CreateExtProcClientHeadersRequest(arena.ptr(), &batch, {}, {}, {},
                                        /*observability_mode=*/true,
                                        /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  EXPECT_TRUE(parsed.observability_mode());
}

TEST_F(CreateExtProcRequestTest, RequestHeadersProtocolConfig) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  ExtProcProcessingMode processing_mode;
  processing_mode.send_request_body = true;
  processing_mode.send_response_body = true;
  std::string serialized =
      CreateExtProcClientHeadersRequest(arena.ptr(), &batch, {}, {}, {},
                                        /*observability_mode=*/false,
                                        /*processing_mode=*/processing_mode)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(
      parsed.protocol_config().request_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
  EXPECT_EQ(
      parsed.protocol_config().response_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
}

TEST_F(CreateExtProcRequestTest,
       ResponseHeadersNeitherAllowedNorDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::string serialized =
      CreateExtProcServerHeadersRequest(arena.ptr(), &batch, {}, {}, {},
                                        /*observability_mode=*/false,
                                        /*processing_mode=*/std::nullopt,
                                        /*end_of_stream=*/true)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  EXPECT_THAT(
      parsed.response_headers().headers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey2, kVal2),
                             IsHeader(kKey3, kVal3)));
  EXPECT_TRUE(parsed.response_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, ResponseHeadersBothAllowedAndDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1).value(),
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::string serialized = CreateExtProcServerHeadersRequest(
                               arena.ptr(), &batch, allowed, disallowed, {},
                               /*observability_mode=*/false,
                               /*processing_mode=*/std::nullopt,
                               /*end_of_stream=*/true)
                               .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  EXPECT_THAT(parsed.response_headers().headers().headers(),
              ::testing::ElementsAre(IsHeader(kKey1, kVal1)));
  EXPECT_TRUE(parsed.response_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, ResponseHeadersOnlyAllowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1).value(),
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::string serialized =
      CreateExtProcServerHeadersRequest(arena.ptr(), &batch, allowed, {}, {},
                                        /*observability_mode=*/false,
                                        /*processing_mode=*/std::nullopt,
                                        /*end_of_stream=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  EXPECT_THAT(
      parsed.response_headers().headers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey3, kVal3)));
  EXPECT_FALSE(parsed.response_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, ResponseHeadersOnlyDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey2).value(),
  };
  std::string serialized =
      CreateExtProcServerHeadersRequest(
          arena.ptr(), &batch, {}, disallowed, {}, /*observability_mode=*/false,
          /*processing_mode=*/std::nullopt, /*end_of_stream=*/true)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  EXPECT_THAT(
      parsed.response_headers().headers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey3, kVal3)));
  EXPECT_TRUE(parsed.response_headers().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, ResponseHeadersObservability) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized =
      CreateExtProcServerHeadersRequest(arena.ptr(), &batch, {}, {}, {},
                                        /*observability_mode=*/true,
                                        /*processing_mode=*/std::nullopt,
                                        /*end_of_stream=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  EXPECT_TRUE(parsed.observability_mode());
}

TEST_F(CreateExtProcRequestTest, ResponseHeadersProtocolConfig) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  ExtProcProcessingMode processing_mode;
  processing_mode.send_request_body = true;
  processing_mode.send_response_body = true;
  std::string serialized =
      CreateExtProcServerHeadersRequest(arena.ptr(), &batch, {}, {}, {},
                                        /*observability_mode=*/false,
                                        /*processing_mode=*/processing_mode,
                                        /*end_of_stream=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(
      parsed.protocol_config().request_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
  EXPECT_EQ(
      parsed.protocol_config().response_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
}

TEST_F(CreateExtProcRequestTest,
       ResponseTrailersNeitherAllowedNorDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::string serialized =
      CreateExtProcServerTrailersRequest(arena.ptr(), &batch, {}, {}, {},
                                         /*observability_mode=*/false,
                                         /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  EXPECT_THAT(
      parsed.response_trailers().trailers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey2, kVal2),
                             IsHeader(kKey3, kVal3)));
}

TEST_F(CreateExtProcRequestTest, ResponseTrailersBothAllowedAndDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1).value(),
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::string serialized = CreateExtProcServerTrailersRequest(
                               arena.ptr(), &batch, allowed, disallowed, {},
                               /*observability_mode=*/false,
                               /*processing_mode=*/std::nullopt)
                               .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  EXPECT_THAT(parsed.response_trailers().trailers().headers(),
              ::testing::ElementsAre(IsHeader(kKey1, kVal1)));
}

TEST_F(CreateExtProcRequestTest, ResponseTrailersOnlyAllowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey1).value(),
      StringMatcher::Create(StringMatcher::Type::kExact, kKey3).value(),
  };
  std::string serialized =
      CreateExtProcServerTrailersRequest(arena.ptr(), &batch, allowed, {}, {},
                                         /*observability_mode=*/false,
                                         /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  EXPECT_THAT(
      parsed.response_trailers().trailers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey3, kVal3)));
}

TEST_F(CreateExtProcRequestTest, ResponseTrailersOnlyDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append(kKey1, Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey2, Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  batch.Append(kKey3, Slice::FromCopiedString(kVal3),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, kKey2).value(),
  };
  std::string serialized = CreateExtProcServerTrailersRequest(
                               arena.ptr(), &batch, {}, disallowed, {},
                               /*observability_mode=*/false,
                               /*processing_mode=*/std::nullopt)
                               .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  EXPECT_THAT(
      parsed.response_trailers().trailers().headers(),
      ::testing::ElementsAre(IsHeader(kKey1, kVal1), IsHeader(kKey3, kVal3)));
}

TEST_F(CreateExtProcRequestTest, ResponseTrailersObservability) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized =
      CreateExtProcServerTrailersRequest(arena.ptr(), &batch, {}, {}, {},
                                         /*observability_mode=*/true,
                                         /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  EXPECT_TRUE(parsed.observability_mode());
}

TEST_F(CreateExtProcRequestTest, ResponseTrailersProtocolConfig) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  ExtProcProcessingMode processing_mode;
  processing_mode.send_request_body = true;
  processing_mode.send_response_body = true;
  std::string serialized =
      CreateExtProcServerTrailersRequest(arena.ptr(), &batch, {}, {}, {},
                                         /*observability_mode=*/false,
                                         /*processing_mode=*/processing_mode)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(
      parsed.protocol_config().request_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
  EXPECT_EQ(
      parsed.protocol_config().response_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
}

TEST_F(CreateExtProcRequestTest, RequestBodyPayloadValid) {
  upb::Arena arena;
  constexpr absl::string_view kBodyData = "test request body data";
  std::string serialized =
      CreateExtProcClientBodyRequest(arena.ptr(), kBodyData, {},
                                     /*observability_mode=*/false,
                                     /*processing_mode=*/std::nullopt,
                                     /*end_of_stream=*/false,
                                     /*end_of_stream_without_message=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_EQ(parsed.request_body().body(), kBodyData);
  EXPECT_FALSE(parsed.request_body().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, RequestBodyEndOfStream) {
  upb::Arena arena;
  constexpr absl::string_view kBodyData = "data";
  std::string serialized =
      CreateExtProcClientBodyRequest(arena.ptr(), kBodyData, {},
                                     /*observability_mode=*/false,
                                     /*processing_mode=*/std::nullopt,
                                     /*end_of_stream=*/true,
                                     /*end_of_stream_without_message=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_EQ(parsed.request_body().body(), kBodyData);
  EXPECT_TRUE(parsed.request_body().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, RequestBodyEndOfStreamWithoutMessage) {
  upb::Arena arena;
  std::string serialized =
      CreateExtProcClientBodyRequest(arena.ptr(), "", {},
                                     /*observability_mode=*/false,
                                     /*processing_mode=*/std::nullopt,
                                     /*end_of_stream=*/true,
                                     /*end_of_stream_without_message=*/true)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_TRUE(parsed.request_body().end_of_stream());
  EXPECT_TRUE(parsed.request_body().end_of_stream_without_message());
}

TEST_F(CreateExtProcRequestTest, RequestBodyObservability) {
  upb::Arena arena;
  std::string serialized =
      CreateExtProcClientBodyRequest(arena.ptr(), "", {},
                                     /*observability_mode=*/true,
                                     /*processing_mode=*/std::nullopt,
                                     /*end_of_stream=*/false,
                                     /*end_of_stream_without_message=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  EXPECT_TRUE(parsed.observability_mode());
}

TEST_F(CreateExtProcRequestTest, RequestBodyProtocolConfig) {
  upb::Arena arena;
  ExtProcProcessingMode processing_mode;
  processing_mode.send_request_body = true;
  processing_mode.send_response_body = true;
  std::string serialized =
      CreateExtProcClientBodyRequest(arena.ptr(), "", {},
                                     /*observability_mode=*/false,
                                     /*processing_mode=*/processing_mode,
                                     /*end_of_stream=*/false,
                                     /*end_of_stream_without_message=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(
      parsed.protocol_config().request_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
  EXPECT_EQ(
      parsed.protocol_config().response_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
}

TEST_F(CreateExtProcRequestTest, ResponseBodyPayloadValid) {
  upb::Arena arena;
  constexpr absl::string_view kBodyData = "test response body data";
  std::string serialized =
      CreateExtProcServerBodyRequest(arena.ptr(), kBodyData, {},
                                     /*observability_mode=*/false,
                                     /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_body());
  EXPECT_EQ(parsed.response_body().body(), kBodyData);
  EXPECT_FALSE(parsed.response_body().end_of_stream());
}

TEST_F(CreateExtProcRequestTest, ResponseBodyObservability) {
  upb::Arena arena;
  std::string serialized =
      CreateExtProcServerBodyRequest(arena.ptr(), "", {},
                                     /*observability_mode=*/true,
                                     /*processing_mode=*/std::nullopt)
          .value();
  auto parsed = ParseRequest(serialized);
  EXPECT_TRUE(parsed.observability_mode());
}

TEST_F(CreateExtProcRequestTest, ResponseBodyProtocolConfig) {
  upb::Arena arena;
  ExtProcProcessingMode processing_mode;
  processing_mode.send_request_body = true;
  processing_mode.send_response_body = true;
  std::string serialized =
      CreateExtProcServerBodyRequest(arena.ptr(), "", {},
                                     /*observability_mode=*/false,
                                     /*processing_mode=*/processing_mode)
          .value();
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(
      parsed.protocol_config().request_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
  EXPECT_EQ(
      parsed.protocol_config().response_body_mode(),
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
}

TEST_F(CreateExtProcRequestTest, AttributesPayload) {
  upb::Arena arena;
  ::google_protobuf_Struct* struct_msg =
      google_protobuf_Struct_new(arena.ptr());
  ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena.ptr());
  ::google_protobuf_Value_set_string_value(
      val_msg, upb_StringView{kVal1.data(), kVal1.size()});
  ::google_protobuf_Struct_fields_set(
      struct_msg, upb_StringView{kKey1.data(), kKey1.size()}, val_msg,
      arena.ptr());
  constexpr absl::string_view kBodyData = "test data";
  std::string serialized =
      CreateExtProcClientBodyRequest(
          arena.ptr(), kBodyData, struct_msg, /*observability_mode=*/false,
          /*processing_mode=*/std::nullopt, /*end_of_stream=*/false,
          /*end_of_stream_without_message=*/false)
          .value();
  auto parsed = ParseRequest(serialized);
  auto it = parsed.attributes().find("envoy.filters.http.ext_proc");
  ASSERT_NE(it, parsed.attributes().end());
  const auto& inner_struct = it->second;
  auto field_it = inner_struct.fields().find(kKey1);
  ASSERT_NE(field_it, inner_struct.fields().end());
  EXPECT_EQ(field_it->second.string_value(), kVal1);
}

//
// CreateExtProcAttributesProtoStruct() tests
//

class CreateExtProcAttributesProtoStructTest : public ::testing::Test {
 protected:
  google::protobuf::Struct ConvertToProto(
      const ::google_protobuf_Struct* upb_struct, upb_Arena* arena) {
    google::protobuf::Struct proto;
    if (upb_struct == nullptr) return proto;
    size_t size;
    char* buf = google_protobuf_Struct_serialize(upb_struct, arena, &size);
    EXPECT_NE(buf, nullptr);
    if (buf != nullptr) {
      EXPECT_TRUE(proto.ParseFromArray(buf, size));
    }
    return proto;
  }
};

TEST_F(CreateExtProcAttributesProtoStructTest, AttributesEmptyRequested) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  auto* upb_struct = CreateExtProcAttributesProtoStruct(arena.ptr(), {}, batch);
  EXPECT_EQ(upb_struct, nullptr);
}

TEST_F(CreateExtProcAttributesProtoStructTest, AttributesAllRecognizedFields) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Set(HttpPathMetadata(), Slice::FromCopiedString("/foo/bar"));
  batch.Set(HttpAuthorityMetadata(),
            Slice::FromCopiedString("host.example.com"));
  batch.Set(HttpMethodMetadata(), HttpMethodMetadata::kGet);
  batch.Append("referer", Slice::FromCopiedString("http://referrer.com"),
               [](absl::string_view, const Slice&) {});
  batch.Append("user-agent", Slice::FromCopiedString("grpc-test-ua"),
               [](absl::string_view, const Slice&) {});
  batch.Append("x-request-id", Slice::FromCopiedString("req-id-123"),
               [](absl::string_view, const Slice&) {});
  std::vector<std::string> requested = {
      "request.path",      "request.url_path", "request.host",
      "request.scheme",    "request.method",   "request.referer",
      "request.useragent", "request.time",     "request.id",
      "request.protocol",  "request.query"};
  auto* upb_struct =
      CreateExtProcAttributesProtoStruct(arena.ptr(), requested, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  EXPECT_EQ(proto.fields().at("request.path").string_value(), "/foo/bar");
  EXPECT_EQ(proto.fields().at("request.url_path").string_value(), "/foo/bar");
  EXPECT_EQ(proto.fields().at("request.host").string_value(),
            "host.example.com");
  EXPECT_EQ(proto.fields().at("request.method").string_value(), "GET");
  EXPECT_EQ(proto.fields().at("request.referer").string_value(),
            "http://referrer.com");
  EXPECT_EQ(proto.fields().at("request.useragent").string_value(),
            "grpc-test-ua");
  EXPECT_EQ(proto.fields().at("request.id").string_value(), "req-id-123");
  EXPECT_EQ(proto.fields().at("request.query").string_value(), "");
  // Unpopulated/omitted keys should not be in fields map
  EXPECT_EQ(proto.fields().find("request.scheme"), proto.fields().end());
  EXPECT_EQ(proto.fields().find("request.time"), proto.fields().end());
  EXPECT_EQ(proto.fields().find("request.protocol"), proto.fields().end());
}

TEST_F(CreateExtProcAttributesProtoStructTest,
       AttributesHostFallbackToHostHeader) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  // No HttpAuthorityMetadata, but has HostMetadata
  batch.Set(HostMetadata(), Slice::FromCopiedString("fallback.host.com"));
  auto* upb_struct =
      CreateExtProcAttributesProtoStruct(arena.ptr(), {"request.host"}, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  EXPECT_EQ(proto.fields().at("request.host").string_value(),
            "fallback.host.com");
}

TEST_F(CreateExtProcAttributesProtoStructTest, AttributesMethodFallbackToPost) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  // No HttpMethodMetadata
  auto* upb_struct = CreateExtProcAttributesProtoStruct(
      arena.ptr(), {"request.method"}, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  EXPECT_EQ(proto.fields().at("request.method").string_value(), "POST");
}

TEST_F(CreateExtProcAttributesProtoStructTest, AttributesRequestHeaders) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("x-custom1", Slice::FromCopiedString(kVal1),
               [](absl::string_view, const Slice&) {});
  batch.Append("x-custom2", Slice::FromCopiedString(kVal2),
               [](absl::string_view, const Slice&) {});
  auto* upb_struct = CreateExtProcAttributesProtoStruct(
      arena.ptr(), {"request.headers"}, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  ASSERT_NE(proto.fields().find("request.headers"), proto.fields().end());
  const auto& headers_struct =
      proto.fields().at("request.headers").struct_value();
  EXPECT_EQ(headers_struct.fields().at("x-custom1").string_value(), kVal1);
  EXPECT_EQ(headers_struct.fields().at("x-custom2").string_value(), kVal2);
}

//
// ExtProcResponse::Parse() tests
//

class ParseExtProcResponseTest : public ::testing::Test {
 protected:
  absl::StatusOr<ExtProcResponse> ParseResponse(
      const envoy::service::ext_proc::v3::ProcessingResponse& response) {
    std::string serialized;
    EXPECT_TRUE(response.SerializeToString(&serialized));
    return ExtProcResponse::Parse(serialized);
  }
};

TEST_F(ParseExtProcResponseTest, ResponseInvalid) {
  // Field 1 (length-delimited) with length 255, but no data.
  auto parsed = ExtProcResponse::Parse("\x0a\xff");
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse ProcessingResponse"));
}

TEST_F(ParseExtProcResponseTest, RequestDrain) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  response.set_request_drain(true);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_TRUE(parsed->request_drain);
}

TEST_F(ParseExtProcResponseTest, UnsupportedResponseCaseRequestTrailers) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_request_trailers();
  auto parsed = ParseResponse(response);
  EXPECT_EQ(
      parsed.status(),
      absl::InternalError("Unsupported ProcessingResponse response case: 5"));
}

TEST_F(ParseExtProcResponseTest, RequestHeadersMutation) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* headers_response = response.mutable_request_headers();
  auto* common_response = headers_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* header_mutation = common_response->mutable_header_mutation();
  auto* set_header = header_mutation->add_set_headers();
  set_header->mutable_header()->set_key(kKey1);
  set_header->mutable_header()->set_raw_value(kVal1);
  header_mutation->add_remove_headers(kKey2);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(
      parsed->response,
      IsRequestHeaders(
          ::testing::ElementsAre(IsHeaderValueOption(
              kKey1, kVal1,
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd)),
          ::testing::ElementsAre(kKey2)));
}

TEST_F(ParseExtProcResponseTest, RequestHeadersUnsupportedStatus) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* headers_response = response.mutable_request_headers();
  auto* common_response = headers_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("CONTINUE_AND_REPLACE is not supported"));
}

TEST_F(ParseExtProcResponseTest, RequestHeadersHeaderMutationEmptyValue) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* headers_response = response.mutable_request_headers();
  auto* common_response = headers_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* header_mutation = common_response->mutable_header_mutation();
  auto* set_header = header_mutation->add_set_headers();
  set_header->mutable_header()->set_key(kKey1);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header error:either value or raw_value "
                                "must be set]"));
}

TEST_F(ParseExtProcResponseTest, RequestHeadersMixedHeaderMutation) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* headers_response = response.mutable_request_headers();
  auto* common_response = headers_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* header_mutation = common_response->mutable_header_mutation();
  auto* set_header1 = header_mutation->add_set_headers();
  set_header1->mutable_header()->set_key("x-valid-key");
  set_header1->mutable_header()->set_raw_value("valid-val");
  constexpr absl::string_view kInvalidHeaderKey = ":path";
  auto* set_header2 = header_mutation->add_set_headers();
  set_header2->mutable_header()->set_key(kInvalidHeaderKey);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError(
                "Failed to parse XdsHeaderValueOption: "
                "[field:header error:either value or raw_value "
                "must be set; field:header.key error:header \":path\" "
                "not allowed]"));
}

TEST_F(ParseExtProcResponseTest, RequestHeadersCommonResponseNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_request_headers();
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(), absl::InternalError("common_response is not set"));
}

TEST_F(ParseExtProcResponseTest, ResponseHeadersMutation) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* headers_response = response.mutable_response_headers();
  auto* common_response = headers_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* header_mutation = common_response->mutable_header_mutation();
  auto* set_header = header_mutation->add_set_headers();
  set_header->mutable_header()->set_key(kKey1);
  set_header->mutable_header()->set_raw_value(kVal1);
  header_mutation->add_remove_headers(kKey2);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(
      parsed->response,
      IsResponseHeaders(
          ::testing::ElementsAre(IsHeaderValueOption(
              kKey1, kVal1,
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd)),
          ::testing::ElementsAre(kKey2)));
}

TEST_F(ParseExtProcResponseTest, ResponseHeadersUnsupportedStatus) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* headers_response = response.mutable_response_headers();
  auto* common_response = headers_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("CONTINUE_AND_REPLACE is not supported"));
}

TEST_F(ParseExtProcResponseTest, ResponseHeadersHeaderMutationEmptyValue) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* headers_response = response.mutable_response_headers();
  auto* common_response = headers_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* header_mutation = common_response->mutable_header_mutation();
  auto* set_header = header_mutation->add_set_headers();
  set_header->mutable_header()->set_key(kKey1);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header error:either value or raw_value "
                                "must be set]"));
}

TEST_F(ParseExtProcResponseTest, ResponseHeadersCommonResponseNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_response_headers();
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(), absl::InternalError("common_response is not set"));
}

TEST_F(ParseExtProcResponseTest, RequestBodyMutation) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_request_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_body("test request body");
  streamed_response->set_end_of_stream(true);
  streamed_response->set_end_of_stream_without_message(false);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->response,
              IsRequestBody("test request body", true, false));
}

TEST_F(ParseExtProcResponseTest, RequestBodyUnsupportedStatus) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_request_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("CONTINUE_AND_REPLACE is not supported"));
}

TEST_F(ParseExtProcResponseTest, RequestBodyCompressedMessageUnsupported) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_request_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_body("test request body");
  streamed_response->set_grpc_message_compressed(true);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("grpc_message_compressed is not supported"));
}

TEST_F(ParseExtProcResponseTest, RequestBodyCommonResponseNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_request_body();
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(), absl::InternalError("common_response is not set"));
}

TEST_F(ParseExtProcResponseTest, RequestBodyBodyMutationNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_request_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(), absl::InternalError("body_mutation is not set"));
}

TEST_F(ParseExtProcResponseTest, RequestBodyStreamedResponseNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_request_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  common_response->mutable_body_mutation();
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("streamed_response is not set"));
}

TEST_F(ParseExtProcResponseTest, ResponseBodyMutation) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_response_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_body("test response body");
  streamed_response->set_end_of_stream(false);
  streamed_response->set_end_of_stream_without_message(false);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->response,
              IsResponseBody("test response body", false, false));
}

TEST_F(ParseExtProcResponseTest, ResponseBodyEndOfStreamRejected) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_response_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_body("test response body");
  streamed_response->set_end_of_stream(true);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("end_of_stream / end_of_stream_without_message "
                                "is not supported for response_body"));
}

TEST_F(ParseExtProcResponseTest,
       ResponseBodyEndOfStreamWithoutMessageRejected) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_response_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_end_of_stream_without_message(true);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("end_of_stream / end_of_stream_without_message "
                                "is not supported for response_body"));
}

TEST_F(ParseExtProcResponseTest, ResponseBodyUnsupportedStatus) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_response_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("CONTINUE_AND_REPLACE is not supported"));
}

TEST_F(ParseExtProcResponseTest, ResponseBodyCompressedMessageUnsupported) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_response_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto* body_mutation = common_response->mutable_body_mutation();
  auto* streamed_response = body_mutation->mutable_streamed_response();
  streamed_response->set_body("test response body");
  streamed_response->set_grpc_message_compressed(true);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("grpc_message_compressed is not supported"));
}

TEST_F(ParseExtProcResponseTest, ResponseBodyCommonResponseNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_response_body();
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(), absl::InternalError("common_response is not set"));
}

TEST_F(ParseExtProcResponseTest, ResponseBodyBodyMutationNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_response_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(), absl::InternalError("body_mutation is not set"));
}

TEST_F(ParseExtProcResponseTest, ResponseBodyStreamedResponseNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* body_response = response.mutable_response_body();
  auto* common_response = body_response->mutable_response();
  common_response->set_status(
      envoy::service::ext_proc::v3::CommonResponse::CONTINUE);
  common_response->mutable_body_mutation();
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("streamed_response is not set"));
}

TEST_F(ParseExtProcResponseTest, ResponseTrailersMutation) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* trailers_response = response.mutable_response_trailers();
  auto* header_mutation = trailers_response->mutable_header_mutation();
  auto* set_header = header_mutation->add_set_headers();
  set_header->mutable_header()->set_key(kKey1);
  set_header->mutable_header()->set_raw_value(kVal1);
  header_mutation->add_remove_headers(kKey2);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(
      parsed->response,
      IsResponseTrailers(
          ::testing::ElementsAre(IsHeaderValueOption(
              kKey1, kVal1,
              XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd)),
          ::testing::ElementsAre(kKey2)));
}

TEST_F(ParseExtProcResponseTest, ResponseTrailersHeaderMutationEmptyValue) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* trailers_response = response.mutable_response_trailers();
  auto* header_mutation = trailers_response->mutable_header_mutation();
  auto* set_header = header_mutation->add_set_headers();
  set_header->mutable_header()->set_key(kKey1);
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("Failed to parse XdsHeaderValueOption: "
                                "[field:header error:either value or raw_value "
                                "must be set]"));
}

TEST_F(ParseExtProcResponseTest, ResponseTrailersHeaderMutationNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  response.mutable_response_trailers();
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(parsed->response,
              IsResponseTrailers(::testing::IsEmpty(), ::testing::IsEmpty()));
}

TEST_F(ParseExtProcResponseTest, ImmediateResponse) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* immediate = response.mutable_immediate_response();
  immediate->mutable_grpc_status()->set_status(16);
  immediate->set_details("invalid credentials");
  auto* header_mutation = immediate->mutable_headers();
  auto* set_header = header_mutation->add_set_headers();
  set_header->mutable_header()->set_key(kKey1);
  set_header->mutable_header()->set_raw_value(kVal1);
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(
      parsed->response,
      IsImmediateResponse(
          GRPC_STATUS_UNAUTHENTICATED, "invalid credentials",
          IsHeaderMutation(
              ::testing::ElementsAre(IsHeaderValueOption(
                  kKey1, kVal1,
                  XdsHeaderValueOption::AppendAction::kAppendIfExistsOrAdd)),
              ::testing::_)));
}

TEST_F(ParseExtProcResponseTest, ImmediateResponseHeaderMutationNull) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* immediate = response.mutable_immediate_response();
  immediate->mutable_grpc_status()->set_status(16);
  immediate->set_details("invalid credentials");
  auto parsed = ParseResponse(response);
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(
      parsed->response,
      IsImmediateResponse(
          GRPC_STATUS_UNAUTHENTICATED, "invalid credentials",
          IsHeaderMutation(::testing::IsEmpty(), ::testing::IsEmpty())));
}

TEST_F(ParseExtProcResponseTest, ImmediateResponseStatusMissing) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* immediate = response.mutable_immediate_response();
  immediate->set_details("invalid credentials");
  auto parsed = ParseResponse(response);
  EXPECT_EQ(parsed.status(),
            absl::InternalError("grpc_status is not set in ImmediateResponse"));
}

TEST_F(ParseExtProcResponseTest, ImmediateResponseStatusInvalid) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse response;
  auto* immediate = response.mutable_immediate_response();
  immediate->mutable_grpc_status()->set_status(99);
  immediate->set_details("invalid credentials");
  auto parsed = ParseResponse(response);
  EXPECT_EQ(
      parsed.status(),
      absl::InternalError("Invalid grpc status code in ImmediateResponse"));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
