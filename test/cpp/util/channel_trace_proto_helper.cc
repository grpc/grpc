/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "test/cpp/util/channel_trace_proto_helper.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/codegen/config.h>
#include <grpcpp/impl/codegen/config_protobuf.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/channelz/channelz.pb.h"

#include "single_include/nlohmann/json.hpp"

using json = nlohmann::json;

namespace grpc {

namespace {

// Generic helper that takes in a json string, converts it to a proto, and
// then back to json. This ensures that the json string was correctly formatted
// according to https://developers.google.com/protocol-buffers/docs/proto3#json
template <typename Message>
void VaidateProtoJsonTranslation(const char* json_c_str) {
  grpc::string json_str(json_c_str);
  Message msg;
  grpc::protobuf::json::JsonParseOptions parse_options;
  // If the following line is failing, then uncomment the last line of the
  // comment, and uncomment the lines that print the two strings. You can
  // then compare the output, and determine what fields are missing.
  //
  // parse_options.ignore_unknown_fields = true;
  grpc::protobuf::util::Status s =
      grpc::protobuf::json::JsonStringToMessage(json_str, &msg, parse_options);
  EXPECT_TRUE(s.ok());
  grpc::string proto_json_str;
  grpc::protobuf::json::JsonPrintOptions print_options;
  // We usually do not want this to be true, however it can be helpful to
  // uncomment and see the output produced then all fields are printed.
  // print_options.always_print_primitive_fields = true;
  s = grpc::protobuf::json::MessageToJsonString(msg, &proto_json_str);
  EXPECT_TRUE(s.ok());
  // uncomment these to compare the json strings.
  // gpr_log(GPR_ERROR, "tracer json: %s", json_str.c_str());
  // gpr_log(GPR_ERROR, "proto  json: %s", proto_json_str.c_str());
  json expected =
      json::parse(json_str, nullptr /* cb */, false /* allow_exceptions */);
  ASSERT_NE(expected, json::value_t::discarded);
  json actual = json::parse(proto_json_str, nullptr /* cb */,
                            false /* allow_exceptions */);
  ASSERT_NE(actual, json::value_t::discarded);
  EXPECT_EQ(expected, actual);
}

}  // namespace

namespace testing {

void ValidateChannelTraceProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::ChannelTrace>(json_c_str);
}

void ValidateChannelProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::Channel>(json_c_str);
}

void ValidateGetTopChannelsResponseProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::GetTopChannelsResponse>(
      json_c_str);
}

void ValidateGetChannelResponseProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::GetChannelResponse>(
      json_c_str);
}

void ValidateGetServerResponseProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::GetServerResponse>(
      json_c_str);
}

void ValidateSubchannelProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::Subchannel>(json_c_str);
}

void ValidateServerProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::Server>(json_c_str);
}

void ValidateGetServersResponseProtoJsonTranslation(const char* json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::GetServersResponse>(
      json_c_str);
}

}  // namespace testing
}  // namespace grpc
