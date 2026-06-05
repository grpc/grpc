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

// Protocol Initiation tests
TEST(ExtProcMessageTest, ClientHeadersProtocolInitiation) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/true, /*send_request_body=*/true,
      /*send_response_body=*/true);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(parsed.protocol_config().request_body_mode(),
            envoy::extensions::filters::http::ext_proc::v3::
                ProcessingMode_BodySendMode_GRPC);
  EXPECT_EQ(parsed.protocol_config().response_body_mode(),
            envoy::extensions::filters::http::ext_proc::v3::
                ProcessingMode_BodySendMode_GRPC);
}

TEST(ExtProcMessageTest,
     ClientHeadersProtocolInitiationSubsequentMessageConfig) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/true,
      /*send_response_body=*/true);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_FALSE(parsed.has_protocol_config());
}

// Client-to-Server Headers tests
TEST(ExtProcMessageTest, ClientHeadersMetadataPropagated) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_request_headers());
  ASSERT_EQ(parsed.request_headers().headers().headers_size(), 2);
  EXPECT_EQ(parsed.request_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.request_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).key(), "key2");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).raw_value(), "val2");
}

TEST(ExtProcMessageTest, ClientHeadersEndOfStreamAlwaysFalse) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_request_headers());
  EXPECT_FALSE(parsed.request_headers().end_of_stream());
}

// Client-to-Server Messages/Body tests
TEST(ExtProcMessageTest, ClientBodyPayloadValid) {
  upb::Arena arena;
  std::string body_data = "test request body data";
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage,
      StdStringToUpbString(body_data), {}, {}, {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/false,
      /*end_of_stream_without_message=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_EQ(parsed.request_body().body(), body_data);
  EXPECT_FALSE(parsed.request_body().end_of_stream());
  EXPECT_FALSE(parsed.request_body().grpc_message_compressed());
}

TEST(ExtProcMessageTest, ClientBodyEndOfStream) {
  upb::Arena arena;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage,
      StdStringToUpbString("data"), {}, {}, {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true,
      /*end_of_stream_without_message=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_TRUE(parsed.request_body().end_of_stream());
}

TEST(ExtProcMessageTest, ClientHalfCloseEndOfStreamWithoutMessage) {
  upb::Arena arena;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientMessage, StdStringToUpbString(""),
      {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true,
      /*end_of_stream_without_message=*/true);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_request_body());
  EXPECT_TRUE(parsed.request_body().end_of_stream());
  EXPECT_TRUE(parsed.request_body().end_of_stream_without_message());
}

// Server-to-Client Events tests
TEST(ExtProcMessageTest, ServerHeadersEndOfStreamTrue) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false, /*end_of_stream=*/true);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_response_headers());
  EXPECT_TRUE(parsed.response_headers().end_of_stream());
}

TEST(ExtProcMessageTest, ServerHeadersProtocolInitiation) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/true, /*send_request_body=*/true,
      /*send_response_body=*/true, /*end_of_stream=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_protocol_config());
  EXPECT_EQ(parsed.protocol_config().request_body_mode(),
            envoy::extensions::filters::http::ext_proc::v3::
                ProcessingMode_BodySendMode_GRPC);
  EXPECT_EQ(parsed.protocol_config().response_body_mode(),
            envoy::extensions::filters::http::ext_proc::v3::
                ProcessingMode_BodySendMode_GRPC);
}

TEST(ExtProcMessageTest, ServerBodyEndOfStreamAlwaysFalse) {
  upb::Arena arena;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerMessage,
      StdStringToUpbString("data"), {}, {}, {}, /*observability_mode=*/false,
      /*is_first_message=*/false,
      /*send_request_body=*/false, /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_response_body());
  EXPECT_FALSE(parsed.response_body().end_of_stream());
}

TEST(ExtProcMessageTest, ServerTrailersMetadataAndStatus) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("grpc-status", Slice::FromCopiedString("13"),
               [](absl::string_view, const Slice&) {});
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kServerTrailers, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_response_trailers());
  ASSERT_EQ(parsed.response_trailers().trailers().headers_size(), 1);
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).key(),
            "grpc-status");
  EXPECT_EQ(parsed.response_trailers().trailers().headers(0).raw_value(), "13");
}

// Metadata & Configuration tests
TEST(ExtProcMessageTest, AttributesExactlyOneExtProcKey) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  auto* attr_struct = ParseAttributes(arena.ptr(), {"request.method"}, batch);
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, attr_struct,
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_EQ(parsed.attributes_size(), 1);
  EXPECT_NE(parsed.attributes().find("envoy.filters.http.ext_proc"),
            parsed.attributes().end());
}

TEST(ExtProcMessageTest, AttributesSpecificMappingMatches) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Set(HttpPathMetadata(), Slice::FromCopiedString("/Service/Method"));
  auto* attr_struct = ParseAttributes(arena.ptr(), {"request.path"}, batch);
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, attr_struct,
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  auto it = parsed.attributes().find("envoy.filters.http.ext_proc");
  ASSERT_NE(it, parsed.attributes().end());
  const auto& struct_val = it->second;
  auto fields_it = struct_val.fields().find("request.path");
  ASSERT_NE(fields_it, struct_val.fields().end());
  EXPECT_EQ(fields_it->second.string_value(), "/Service/Method");
}

TEST(ExtProcMessageTest, PopulateAttributesMapAllCELVariables) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Set(HttpPathMetadata(), Slice::FromCopiedString("/MyService/CallItem"));
  batch.Set(HttpAuthorityMetadata(),
            Slice::FromCopiedString("peer-host.internal"));
  batch.Set(HttpMethodMetadata(), HttpMethodMetadata::ValueType::kGet);
  batch.Append("referer", Slice::FromCopiedString("https://google.com"),
               [](absl::string_view, const Slice&) {});
  batch.Append("user-agent", Slice::FromCopiedString("gRPC-C++/1.0"),
               [](absl::string_view, const Slice&) {});
  batch.Append("x-request-id", Slice::FromCopiedString("req-uuid-1234"),
               [](absl::string_view, const Slice&) {});
  std::vector<std::string> requested = {
      "request.path",    "request.url_path",  "request.host",
      "request.scheme",  "request.method",    "request.headers",
      "request.referer", "request.useragent", "request.time",
      "request.id",      "request.protocol",  "request.query"};
  auto* attr_struct = ParseAttributes(arena.ptr(), requested, batch);
  ASSERT_NE(attr_struct, nullptr);
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, attr_struct,
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  auto it = parsed.attributes().find("envoy.filters.http.ext_proc");
  ASSERT_NE(it, parsed.attributes().end());
  const auto& struct_val = it->second;
  ASSERT_EQ(struct_val.fields().size(), 9);
  EXPECT_EQ(struct_val.fields().at("request.path").string_value(),
            "/MyService/CallItem");
  EXPECT_EQ(struct_val.fields().at("request.url_path").string_value(),
            "/MyService/CallItem");
  EXPECT_EQ(struct_val.fields().at("request.host").string_value(),
            "peer-host.internal");
  EXPECT_EQ(struct_val.fields().find("request.scheme"),
            struct_val.fields().end());
  EXPECT_EQ(struct_val.fields().at("request.method").string_value(), "GET");
  EXPECT_TRUE(struct_val.fields().at("request.headers").has_struct_value());
  EXPECT_EQ(struct_val.fields().at("request.referer").string_value(),
            "https://google.com");
  EXPECT_EQ(struct_val.fields().at("request.useragent").string_value(),
            "gRPC-C++/1.0");
  EXPECT_EQ(struct_val.fields().find("request.time"),
            struct_val.fields().end());
  EXPECT_EQ(struct_val.fields().at("request.id").string_value(),
            "req-uuid-1234");
  EXPECT_EQ(struct_val.fields().find("request.protocol"),
            struct_val.fields().end());
  EXPECT_EQ(struct_val.fields().at("request.query").string_value(), "");
}

TEST(ExtProcMessageTest, ObservabilityModeTrue) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, {},
      /*observability_mode=*/true,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_TRUE(parsed.observability_mode());
}

TEST(ExtProcMessageTest, ObservabilityModeFalse) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, {}, {}, {},
      /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  EXPECT_FALSE(parsed.observability_mode());
}

TEST(ExtProcMessageTest, HeaderResponseStatusContinue) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* headers_resp = proto_resp.mutable_request_headers();
  headers_resp->mutable_response()->set_status(
      envoy::service::ext_proc::v3::CommonResponse_ResponseStatus_CONTINUE);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_headers.has_value());
}

TEST(ExtProcMessageTest, HeaderResponseContinueAndReplaceFails) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* headers_resp = proto_resp.mutable_request_headers();
  headers_resp->mutable_response()->set_status(
      envoy::service::ext_proc::v3::
          CommonResponse_ResponseStatus_CONTINUE_AND_REPLACE);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_headers.has_value());
  EXPECT_FALSE(result->request_headers->ok());
  EXPECT_EQ(result->request_headers->status().code(), absl::StatusCode::kInternal);
  EXPECT_EQ(result->request_headers->status().message(),
            "CONTINUE_AND_REPLACE is not supported");
}

TEST(ExtProcMessageTest, HeaderResponseHeaderMutations) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* headers_resp = proto_resp.mutable_request_headers();
  auto* mutation = headers_resp->mutable_response()->mutable_header_mutation();
  auto* set_header = mutation->add_set_headers();
  set_header->mutable_header()->set_key("custom-header");
  set_header->mutable_header()->set_value("custom-value");
  mutation->add_remove_headers("remove-header");
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_headers.has_value());
  ASSERT_TRUE(result->request_headers->ok());
  ASSERT_EQ(result->request_headers->value().set_headers.size(), 1);
  EXPECT_EQ(result->request_headers->value().set_headers[0].header.first,
            "custom-header");
  EXPECT_EQ(result->request_headers->value().set_headers[0].header.second,
            "custom-value");
  ASSERT_EQ(result->request_headers->value().remove_headers.size(), 1);
  EXPECT_EQ(result->request_headers->value().remove_headers[0],
            "remove-header");
}

TEST(ExtProcMessageTest, HeaderResponseIgnoredFields) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* headers_resp = proto_resp.mutable_request_headers();
  headers_resp->mutable_response()->mutable_body_mutation()->set_body(
      "ignored-body");
  headers_resp->mutable_response()->mutable_trailers()->add_headers()->set_key(
      "ignored-trailer");
  headers_resp->mutable_response()->set_clear_route_cache(true);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result->request_body.has_value());
  EXPECT_FALSE(result->response_trailers.has_value());
}

TEST(ExtProcMessageTest, BodyResponseMessageReplacement) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* body_resp = proto_resp.mutable_request_body();
  body_resp->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_body("replaced-body");
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_body.has_value());
  ASSERT_TRUE(result->request_body->ok());
  EXPECT_EQ((*result->request_body)->body, "replaced-body");
}

TEST(ExtProcMessageTest, BodyResponseEndOfStreamHonored) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* body_resp = proto_resp.mutable_request_body();
  body_resp->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_end_of_stream(true);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_body.has_value());
  ASSERT_TRUE(result->request_body->ok());
  EXPECT_TRUE((*result->request_body)->end_of_stream);
}

TEST(ExtProcMessageTest, BodyResponseEndOfStreamWithoutMessageHonored) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* body_resp = proto_resp.mutable_request_body();
  body_resp->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_end_of_stream_without_message(true);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_body.has_value());
  ASSERT_TRUE(result->request_body->ok());
  EXPECT_TRUE((*result->request_body)->end_of_stream_without_message);
}

TEST(ExtProcMessageTest, BodyResponseCompressionViolationFails) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* body_resp = proto_resp.mutable_request_body();
  body_resp->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_grpc_message_compressed(true);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_body.has_value());
  EXPECT_FALSE(result->request_body->ok());
  EXPECT_EQ(result->request_body->status().message(),
            "grpc_message_compressed is not supported");
}

TEST(ExtProcMessageTest, BodyMutationBodyValueIgnored) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* body_resp = proto_resp.mutable_request_body();
  body_resp->mutable_response()->mutable_body_mutation()->set_body(
      "unsupported-raw-body");
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_body.has_value());
  ASSERT_TRUE(result->request_body->ok());
  EXPECT_TRUE((*result->request_body)->body.empty());
}

TEST(ExtProcMessageTest, BodyMutationClearBodyIgnored) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* body_resp = proto_resp.mutable_request_body();
  body_resp->mutable_response()->mutable_body_mutation()->set_clear_body(true);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->request_body.has_value());
  ASSERT_TRUE(result->request_body->ok());
  EXPECT_TRUE((*result->request_body)->body.empty());
}

TEST(ExtProcMessageTest, BodyResponseIgnoredFields) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* body_resp = proto_resp.mutable_request_body();
  body_resp->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_body("test");
  body_resp->mutable_response()
      ->mutable_header_mutation()
      ->add_set_headers()
      ->mutable_header()
      ->set_key("ignored");
  body_resp->mutable_response()->mutable_trailers()->add_headers()->set_key(
      "ignored");
  body_resp->mutable_response()->set_clear_route_cache(true);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result->request_headers.has_value());
  EXPECT_FALSE(result->response_trailers.has_value());
}

TEST(ExtProcMessageTest, TrailersResponseMutationSupport) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* trailers_resp = proto_resp.mutable_response_trailers();
  auto* set_header =
      trailers_resp->mutable_header_mutation()->add_set_headers();
  set_header->mutable_header()->set_key("final-trailer");
  set_header->mutable_header()->set_value("final-value");
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->response_trailers.has_value());
  ASSERT_EQ(result->response_trailers->set_headers.size(), 1);
  EXPECT_EQ(result->response_trailers->set_headers[0].header.first,
            "final-trailer");
  EXPECT_EQ(result->response_trailers->set_headers[0].header.second,
            "final-value");
}

TEST(ExtProcMessageTest, ImmediateResponseStatusAndDetails) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* imm_resp = proto_resp.mutable_immediate_response();
  imm_resp->mutable_status()->set_code(
      envoy::type::v3::StatusCode::Unauthorized);
  imm_resp->set_details("rejected by authn server");
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->immediate_response.has_value());
  EXPECT_EQ(result->immediate_response->details, "rejected by authn server");
}

TEST(ExtProcMessageTest, ImmediateResponseHeaderBestEffort) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* imm_resp = proto_resp.mutable_immediate_response();
  auto* header =
      imm_resp->mutable_headers()->add_set_headers()->mutable_header();
  header->set_key("x-best-effort");
  header->set_value("mapped-in-cancellation");
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->immediate_response.has_value());
  ASSERT_EQ(result->immediate_response->header_mutation.set_headers.size(), 1);
  EXPECT_EQ(
      result->immediate_response->header_mutation.set_headers[0].header.first,
      "x-best-effort");
  EXPECT_EQ(
      result->immediate_response->header_mutation.set_headers[0].header.second,
      "mapped-in-cancellation");
}

TEST(ExtProcMessageTest, DrainHalfCloseTriggered) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  proto_resp.set_request_drain(true);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/false);
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->request_drain);
}

TEST(ExtProcMessageTest, ObservabilityModeResponseIgnored) {
  upb::Arena arena;
  envoy::service::ext_proc::v3::ProcessingResponse proto_resp;
  auto* headers_resp = proto_resp.mutable_request_headers();
  headers_resp->mutable_response()->set_status(
      envoy::service::ext_proc::v3::CommonResponse_ResponseStatus_CONTINUE);
  std::string serialized = proto_resp.SerializeAsString();
  auto* upb_resp = envoy_service_ext_proc_v3_ProcessingResponse_parse(
      serialized.data(), serialized.size(), arena.ptr());
  ASSERT_NE(upb_resp, nullptr);
  auto result = ParseExtProcResponse(upb_resp, /*observability_mode=*/true);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result->request_headers.has_value());
}

TEST(ExtProcMessageTest, AllowedHeadersForwardedOthersDropped) {
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
  std::vector<StringMatcher> disallowed;
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, allowed,
      disallowed, {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_request_headers());
  ASSERT_EQ(parsed.request_headers().headers().headers_size(), 2);
  EXPECT_EQ(parsed.request_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.request_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).raw_value(), "val3");
}

TEST(ExtProcMessageTest, DisallowedHeadersDropped) {
  upb::Arena arena;
  grpc_metadata_batch batch;
  batch.Append("key1", Slice::FromCopiedString("val1"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key2", Slice::FromCopiedString("val2"),
               [](absl::string_view, const Slice&) {});
  batch.Append("key3", Slice::FromCopiedString("val3"),
               [](absl::string_view, const Slice&) {});
  std::vector<StringMatcher> allowed;
  std::vector<StringMatcher> disallowed = {
      StringMatcher::Create(StringMatcher::Type::kExact, "key2").value(),
  };
  std::string serialized = CreateExtProcRequest(
      arena.ptr(), ExtProcRequestType::kClientHeaders, &batch, allowed,
      disallowed, {}, /*observability_mode=*/false,
      /*is_first_message=*/false, /*send_request_body=*/false,
      /*send_response_body=*/false);
  envoy::service::ext_proc::v3::ProcessingRequest parsed;
  ASSERT_TRUE(parsed.ParseFromString(serialized));
  ASSERT_TRUE(parsed.has_request_headers());
  ASSERT_EQ(parsed.request_headers().headers().headers_size(), 2);
  EXPECT_EQ(parsed.request_headers().headers().headers(0).key(), "key1");
  EXPECT_EQ(parsed.request_headers().headers().headers(0).raw_value(), "val1");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).key(), "key3");
  EXPECT_EQ(parsed.request_headers().headers().headers(1).raw_value(), "val3");
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
