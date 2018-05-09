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

#include "test/cpp/util/channel_trace_proto_helper.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/channelz/channelz.pb.h"

namespace grpc {
namespace testing {

namespace {

// Generic helper that takes in a json string, converts it to a proto, and
// then back to json. This ensures that the json string was correctly formatted
// according to https://developers.google.com/protocol-buffers/docs/proto3#json
template <typename Message>
void VaidateProtoJsonTranslation(char* json_c_str) {
  std::string json_str(json_c_str);
  Message msg;
  google::protobuf::util::JsonParseOptions parse_options;
  // If the following line is failing, then uncomment the last line of the
  // comment, and uncomment the lines that print the two strings. You can
  // then compare the output, and determine what fields are missing.
  //
  // parse_options.ignore_unknown_fields = true;
  ASSERT_EQ(google::protobuf::util::JsonStringToMessage(json_str, &msg,
                                                        parse_options),
            google::protobuf::util::Status::OK);
  std::string proto_json_str;
  ASSERT_EQ(google::protobuf::util::MessageToJsonString(msg, &proto_json_str),
            google::protobuf::util::Status::OK);
  // uncomment these to compare the the json strings.
  // gpr_log(GPR_ERROR, "tracer json: %s", json_str.c_str());
  // gpr_log(GPR_ERROR, "proto  json: %s", proto_json_str.c_str());
  ASSERT_EQ(json_str, proto_json_str);
}

}  // namespace

void ValidateChannelTraceProtoJsonTranslation(char* tracer_json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::ChannelTrace>(
      tracer_json_c_str);
}

void ValidateChannelProtoJsonTranslation(char* channel_json_c_str) {
  VaidateProtoJsonTranslation<grpc::channelz::v1::Channel>(channel_json_c_str);
}

}  // namespace testing
}  // namespace grpc
