/*
 *
 * Copyright 2016 gRPC authors.
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

#include "test/cpp/qps/parse_json.h"

#include <string>

#include <grpc/support/log.h>

namespace grpc {
namespace testing {

void ParseJson(const std::string& json, const std::string& type,
               GRPC_CUSTOM_MESSAGE* msg) {
  std::unique_ptr<protobuf::json::TypeResolver> type_resolver(
      protobuf::json::NewTypeResolverForDescriptorPool(
          "type.googleapis.com", protobuf::DescriptorPool::generated_pool()));
  std::string binary;
  auto status = JsonToBinaryString(
      type_resolver.get(), "type.googleapis.com/" + type, json, &binary);
  if (!status.ok()) {
    std::string errmsg(status.message());
    gpr_log(GPR_ERROR, "Failed to convert json to binary: errcode=%d msg=%s",
            status.code(), errmsg.c_str());
    gpr_log(GPR_ERROR, "JSON: %s", json.c_str());
    abort();
  }
  GPR_ASSERT(msg->ParseFromString(binary));
}

std::string SerializeJson(const GRPC_CUSTOM_MESSAGE& msg,
                          const std::string& type) {
  std::unique_ptr<protobuf::json::TypeResolver> type_resolver(
      protobuf::json::NewTypeResolverForDescriptorPool(
          "type.googleapis.com", protobuf::DescriptorPool::generated_pool()));
  std::string binary;
  std::string json_string;
  msg.SerializeToString(&binary);
  auto status =
      BinaryToJsonString(type_resolver.get(), type, binary, &json_string);
  GPR_ASSERT(status.ok());
  return json_string;
}

}  // namespace testing
}  // namespace grpc
