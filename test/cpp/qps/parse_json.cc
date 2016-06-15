/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
