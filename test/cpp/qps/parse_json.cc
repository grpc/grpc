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

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/type_resolver_util.h>
#include <grpc/support/log.h>

namespace grpc {
namespace testing {

void ParseJson(const grpc::string& json, const grpc::string& type,
               GRPC_CUSTOM_MESSAGE* msg) {
  std::unique_ptr<google::protobuf::util::TypeResolver> type_resolver(
      google::protobuf::util::NewTypeResolverForDescriptorPool(
          "type.googleapis.com",
          google::protobuf::DescriptorPool::generated_pool()));
  grpc::string binary;
  auto status = JsonToBinaryString(
      type_resolver.get(), "type.googleapis.com/" + type, json, &binary);
  if (!status.ok()) {
    grpc::string errmsg(status.error_message());
    gpr_log(GPR_ERROR, "Failed to convert json to binary: errcode=%d msg=%s",
            status.error_code(), errmsg.c_str());
    gpr_log(GPR_ERROR, "JSON: %s", json.c_str());
    abort();
  }
  GPR_ASSERT(msg->ParseFromString(binary));
}

grpc::string SerializeJson(const GRPC_CUSTOM_MESSAGE& msg,
                           const grpc::string& type) {
  std::unique_ptr<google::protobuf::util::TypeResolver> type_resolver(
      google::protobuf::util::NewTypeResolverForDescriptorPool(
          "type.googleapis.com",
          google::protobuf::DescriptorPool::generated_pool()));
  grpc::string binary;
  grpc::string json_string;
  msg.SerializeToString(&binary);
  auto status =
      BinaryToJsonString(type_resolver.get(), type, binary, &json_string);
  GPR_ASSERT(status.ok());
  return json_string;
}

}  // testing
}  // grpc
