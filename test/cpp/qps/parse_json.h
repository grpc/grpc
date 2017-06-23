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

#ifndef TEST_QPS_PARSE_JSON_H
#define TEST_QPS_PARSE_JSON_H

#include <grpc++/impl/codegen/config_protobuf.h>
#include <grpc++/support/config.h>

namespace grpc {
namespace testing {

void ParseJson(const grpc::string& json, const grpc::string& type,
               GRPC_CUSTOM_MESSAGE* msg);

grpc::string SerializeJson(const GRPC_CUSTOM_MESSAGE& msg,
                           const grpc::string& type);

}  // testing
}  // grpc

#endif  // TEST_QPS_PARSE_JSON_H
