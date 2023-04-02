// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_JSON_JSON_CHANNEL_ARGS_H
#define GRPC_SRC_CORE_LIB_JSON_JSON_CHANNEL_ARGS_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/json/json_args.h"

namespace grpc_core {

class JsonChannelArgs : public JsonArgs {
 public:
  explicit JsonChannelArgs(const ChannelArgs& args) : args_(args) {}

  bool IsEnabled(absl::string_view key) const override {
    return args_.GetBool(key).value_or(false);
  }

 private:
  ChannelArgs args_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_JSON_JSON_CHANNEL_ARGS_H
