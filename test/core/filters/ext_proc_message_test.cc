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

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/processing_mode.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "src/core/ext/filters/ext_proc/ext_proc_messages.h"
#include "src/core/util/upb_utils.h"
#include "upb/mem/arena.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace {

class ExtProcRequestTest : public ::testing::Test {
 protected:
  envoy::service::ext_proc::v3::ProcessingRequest ParseRequest(
      const std::string& serialized) {
    envoy::service::ext_proc::v3::ProcessingRequest parsed;
    EXPECT_TRUE(parsed.ParseFromString(serialized));
    return parsed;
  }
};

TEST_F(ExtProcRequestTest, RequestHeadersForwardingNeitherSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  ASSERT_EQ(parsed.request_headers().headers().headers_size(), 3);
  EXPECT_EQ(parsed.request_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.request_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).key(), "key2");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).raw_value(), "val2");
  EXPECT_EQ(parsed.request_headers().headers().headers(2).key(), "key3");
  EXPECT_EQ(parsed.request_headers().headers().headers(2).raw_value(), "val3");
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, RequestHeadersForwardingBothSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key1").value(),
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, allowed,
      disallowed, {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  ASSERT_EQ(parsed.request_headers().headers().headers_size(), 1);
  EXPECT_EQ(parsed.request_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.request_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, RequestHeadersForwardingOnlyAllowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key1").value(),
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, allowed, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  ASSERT_EQ(parsed.request_headers().headers().headers_size(), 2);
  EXPECT_EQ(parsed.request_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.request_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).raw_value(), "val3");
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, RequestHeadersForwardingOnlyDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key2").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, disallowed,
      {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_headers());
  ASSERT_EQ(parsed.request_headers().headers().headers_size(), 2);
  EXPECT_EQ(parsed.request_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.request_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).raw_value(), "val3");
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, ResponseHeadersForwardingNeitherSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  ASSERT_EQ(parsed.response_headers().headers().headers_size(), 3);
  EXPECT_EQ(parsed.response_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.response_headers().headers().headers(1).key(), "key2");
  EXPECT_EQ(parsed.response_headers().headers().headers(1).raw_value(), "val2");
  EXPECT_EQ(parsed.response_headers().headers().headers(2).key(), "key3");
  EXPECT_EQ(parsed.response_headers().headers().headers(2).raw_value(), "val3");
  EXPECT_TRUE(parsed.response_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, ResponseHeadersForwardingBothSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key1").value(),
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerHeaders, &batch, allowed,
      disallowed, {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  ASSERT_EQ(parsed.response_headers().headers().headers_size(), 1);
  EXPECT_EQ(parsed.response_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_TRUE(parsed.response_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, ResponseHeadersForwardingOnlyAllowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key1").value(),
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerHeaders, &batch, allowed, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  ASSERT_EQ(parsed.response_headers().headers().headers_size(), 2);
  EXPECT_EQ(parsed.response_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.response_headers().headers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.response_headers().headers().headers(1).raw_value(), "val3");
  EXPECT_FALSE(parsed.response_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, ResponseHeadersForwardingOnlyDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key2").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerHeaders, &batch, {}, disallowed,
      {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_headers());
  ASSERT_EQ(parsed.response_headers().headers().headers_size(), 2);
  EXPECT_EQ(parsed.response_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.response_headers().headers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.response_headers().headers().headers(1).raw_value(), "val3");
  EXPECT_TRUE(parsed.response_headers().end_of_stream());
}

TEST_F(ExtProcRequestTest, ResponseTrailersForwardingNeitherSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerTrailers, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  ASSERT_EQ(parsed.response_trailers().trailers().headers_size(), 3);
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(1).key(), "key2");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(1).raw_value(), "val2");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(2).key(), "key3");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(2).raw_value(), "val3");
}

TEST_F(ExtProcRequestTest, ResponseTrailersForwardingBothSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key1").value(),
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerTrailers, &batch, allowed,
      disallowed, {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  ASSERT_EQ(parsed.response_trailers().trailers().headers_size(), 1);
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).raw_value(), "val1");
}

TEST_F(ExtProcRequestTest, ResponseTrailersForwardingOnlyAllowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key1").value(),
      StringMatcher::Create(StringMatcher::Type::kExact, "key3").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerTrailers, &batch, allowed, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  ASSERT_EQ(parsed.response_trailers().trailers().headers_size(), 2);
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(1).raw_value(), "val3");
}

TEST_F(ExtProcRequestTest, ResponseTrailersForwardingOnlyDisallowedSet) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key2").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerTrailers, &batch, {}, disallowed,
      {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_trailers());
  ASSERT_EQ(parsed.response_trailers().trailers().headers_size(), 2);
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(1).raw_value(), "val3");
}

TEST_F(ExtProcRequestTest, RequestBodyPayloadValid) {
  upb::Arena arena;
  std::string body_data = "test request body data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/false,
      /*end_of_stream_without_message=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_EQ(parsed.request_body().body(), body_data);
  EXPECT_FALSE(parsed.request_body().end_of_stream());
}

TEST_F(ExtProcRequestTest, RequestBodyEndOfStream) {
  upb::Arena arena;
  std::string body_data = "data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true,
      /*end_of_stream_without_message=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_EQ(parsed.request_body().body(), body_data);
  EXPECT_TRUE(parsed.request_body().end_of_stream());
}

TEST_F(ExtProcRequestTest, RequestBodyEndOfStreamWithoutMessage) {
  upb::Arena arena;
  upb_StringView payload{"", 0};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true,
      /*end_of_stream_without_message=*/true);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_TRUE(parsed.request_body().end_of_stream());
  EXPECT_TRUE(parsed.request_body().end_of_stream_without_message());
}

TEST_F(ExtProcRequestTest, ResponseBodyPayloadValid) {
  upb::Arena arena;
  std::string body_data = "test response body data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerMessage, payload, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_response_body());
  EXPECT_EQ(parsed.response_body().body(), body_data);
  EXPECT_FALSE(parsed.response_body().end_of_stream());
}

TEST_F(ExtProcRequestTest, ObservabilityModeOn) {
  upb::Arena arena;
  std::string body_data = "test data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {}, {},
      /*observability_mode=*/true,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  EXPECT_TRUE(parsed.observability_mode());
}

TEST_F(ExtProcRequestTest, ObservabilityModeOff) {
  upb::Arena arena;
  std::string body_data = "test data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  EXPECT_FALSE(parsed.observability_mode());
}

TEST_F(ExtProcRequestTest, ProtocolConfigFirstMessage) {
  upb::Arena arena;
  std::string body_data = "test data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/true, /*send_request_body=*/true,
      /*send_response_body=*/true);
  auto parsed = ParseRequest(serialized);
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(parsed.protocol_config().request_body_mode(),
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
  EXPECT_EQ(parsed.protocol_config().response_body_mode(),
            envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
}

TEST_F(ExtProcRequestTest, ProtocolConfigNotFirstMessage) {
  upb::Arena arena;
  std::string body_data = "test data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/true,
      /*send_response_body=*/true);
  auto parsed = ParseRequest(serialized);
  EXPECT_FALSE(parsed.has_protocol_config());
}

TEST_F(ExtProcRequestTest, AttributesPayload) {
  upb::Arena arena;
  ::google_protobuf_Struct* struct_msg = google_protobuf_Struct_new(arena.ptr());
  std::string key = "key1";
  std::string val = "val1";
  ::google_protobuf_Value* val_msg = ::google_protobuf_Value_new(arena.ptr());
  ::google_protobuf_Value_set_string_value(
      val_msg, upb_StringView{val.data(), val.size()});
  ::google_protobuf_Struct_fields_set(
      struct_msg, upb_StringView{key.data(), key.size()}, val_msg, arena.ptr());
  std::string body_data = "test data";
  upb_StringView payload{body_data.data(), body_data.size()};
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, payload, {}, {},
      struct_msg, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  auto parsed = ParseRequest(serialized);
  auto it = parsed.attributes().find("envoy.filters.http.ext_proc");
  ASSERT_NE(it, parsed.attributes().end());
  const auto& inner_struct = it->second;
  auto field_it = inner_struct.fields().find("key1");
  ASSERT_NE(field_it, inner_struct.fields().end());
  EXPECT_EQ(field_it->second.string_value(), "val1");
}

class ParseAttributesTest : public ::testing::Test {
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

TEST_F(ParseAttributesTest, AttributesEmptyRequested) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  auto* upb_struct = ParseAttributes(arena.ptr(), {}, batch);
  EXPECT_EQ(upb_struct, nullptr);
}

TEST_F(ParseAttributesTest, AttributesAllRecognizedFields) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Set(HttpPathMetadata(), Slice::FromCopiedString("/foo/bar"));
  batch.Set(HttpAuthorityMetadata(), Slice::FromCopiedString("host.example.com"));
  batch.Set(HttpMethodMetadata(), HttpMethodMetadata::kGet);
  batch.Append("referer", Slice::FromCopiedString("http://referrer.com"),
               [](absl::string_view, const Slice&) {});
  batch.Append("user-agent", Slice::FromCopiedString("grpc-test-ua"),
               [](absl::string_view, const Slice&) {});
  batch.Append("x-request-id", Slice::FromCopiedString("req-id-123"),
               [](absl::string_view, const Slice&) {});
  std::vector<std::string> requested = {
      "request.path", "request.url_path", "request.host", "request.scheme",
      "request.method", "request.referer", "request.useragent", "request.time",
      "request.id", "request.protocol", "request.query"
  };
  auto* upb_struct = ParseAttributes(arena.ptr(), requested, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  EXPECT_EQ(proto.fields().at("request.path").string_value(), "/foo/bar");
  EXPECT_EQ(proto.fields().at("request.url_path").string_value(), "/foo/bar");
  EXPECT_EQ(proto.fields().at("request.host").string_value(), "host.example.com");
  EXPECT_EQ(proto.fields().at("request.method").string_value(), "GET");
  EXPECT_EQ(proto.fields().at("request.referer").string_value(), "http://referrer.com");
  EXPECT_EQ(proto.fields().at("request.useragent").string_value(), "grpc-test-ua");
  EXPECT_EQ(proto.fields().at("request.id").string_value(), "req-id-123");
  EXPECT_EQ(proto.fields().at("request.query").string_value(), "");
  // Unpopulated/omitted keys should not be in fields map
  EXPECT_EQ(proto.fields().find("request.scheme"), proto.fields().end());
  EXPECT_EQ(proto.fields().find("request.time"), proto.fields().end());
  EXPECT_EQ(proto.fields().find("request.protocol"), proto.fields().end());
}

TEST_F(ParseAttributesTest, AttributesHostFallbackToHostHeader) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  // No HttpAuthorityMetadata, but has HostMetadata
  batch.Set(HostMetadata(), Slice::FromCopiedString("fallback.host.com"));
  auto* upb_struct = ParseAttributes(arena.ptr(), {"request.host"}, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  EXPECT_EQ(proto.fields().at("request.host").string_value(), "fallback.host.com");
}

TEST_F(ParseAttributesTest, AttributesMethodFallbackToPost) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  // No HttpMethodMetadata
  auto* upb_struct = ParseAttributes(arena.ptr(), {"request.method"}, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  EXPECT_EQ(proto.fields().at("request.method").string_value(), "POST");
}

TEST_F(ParseAttributesTest, AttributesRequestHeaders) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("x-custom1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("x-custom2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  auto* upb_struct = ParseAttributes(arena.ptr(), {"request.headers"}, batch);
  ASSERT_NE(upb_struct, nullptr);
  auto proto = ConvertToProto(upb_struct, arena.ptr());
  ASSERT_NE(proto.fields().find("request.headers"), proto.fields().end());
  const auto& headers_struct = proto.fields().at("request.headers").struct_value();
  EXPECT_EQ(headers_struct.fields().at("x-custom1").string_value(), "val1");
  EXPECT_EQ(headers_struct.fields().at("x-custom2").string_value(), "val2");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
