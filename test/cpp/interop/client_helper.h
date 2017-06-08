/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_TEST_CPP_INTEROP_CLIENT_HELPER_H
#define GRPC_TEST_CPP_INTEROP_CLIENT_HELPER_H

#include <memory>
#include <unordered_map>

#include <grpc++/channel.h>

#include "src/core/lib/surface/call_test_only.h"

namespace grpc {
namespace testing {

grpc::string GetServiceAccountJsonKey();

grpc::string GetOauth2AccessToken();

void UpdateActions(
    std::unordered_map<grpc::string, std::function<bool()>>* actions);

std::shared_ptr<Channel> CreateChannelForTestCase(
    const grpc::string& test_case);

class InteropClientContextInspector {
 public:
  InteropClientContextInspector(const ::grpc::ClientContext& context)
      : context_(context) {}

  // Inspector methods, able to peek inside ClientContext, follow.
  grpc_compression_algorithm GetCallCompressionAlgorithm() const {
    return grpc_call_test_only_get_compression_algorithm(context_.call_);
  }

  uint32_t GetMessageFlags() const {
    return grpc_call_test_only_get_message_flags(context_.call_);
  }

 private:
  const ::grpc::ClientContext& context_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_INTEROP_CLIENT_HELPER_H
