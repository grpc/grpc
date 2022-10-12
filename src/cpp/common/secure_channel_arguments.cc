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

#include <memory>
#include <string>
#include <vector>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/config.h>

namespace grpc {

void ChannelArguments::SetSslTargetNameOverride(const std::string& name) {
  SetString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, name);
}

std::string ChannelArguments::GetSslTargetNameOverride() const {
  for (unsigned int i = 0; i < args_.size(); i++) {
    if (std::string(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG) == args_[i].key) {
      return args_[i].value.string;
    }
  }
  return "";
}

}  // namespace grpc
