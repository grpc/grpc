//
//
// Copyright 2017 gRPC authors.
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
//

#include "src/core/channelz/channel_trace.h"

#include <grpc/support/alloc.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <utility>

#include "absl/strings/str_cat.h"
#include "src/core/channelz/channelz.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace channelz {

//
// ChannelTrace
//

Json ChannelTrace::RenderJson() const {
  Node* node = root_.Get();
  if (node == nullptr) return Json();  // JSON null
  Json::Array array;
  node->ForEachTraceEvent(
      [&array](gpr_timespec timestamp, int indent, absl::string_view line) {
        Json::Object object = {
            {"severity", Json::FromString("CT_INFO")},
            {"timestamp", Json::FromString(gpr_format_timespec(timestamp))},
            {"description",
             Json::FromString(absl::StrCat(std::string(' ', indent), line))},
        };
        array.push_back(Json::FromObject(std::move(object)));
      },
      -1);
  Json::Object object;
  if (!array.empty()) {
    object["events"] = Json::FromArray(std::move(array));
  }
  return Json::FromObject(std::move(object));
}

}  // namespace channelz
}  // namespace grpc_core
