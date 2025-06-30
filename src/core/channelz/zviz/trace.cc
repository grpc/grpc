// Copyright 2025 The gRPC Authors
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

#include "src/core/channelz/zviz/trace.h"

#include "src/core/channelz/zviz/data.h"

namespace grpc_zviz {

void Format(Environment& env, const grpc::channelz::v2::TraceEvent& trace_event,
            layout::Table& trace_table) {
  trace_table.AppendColumn().AppendTimestamp(trace_event.timestamp());
  auto& event = trace_table.AppendColumn();
  if (!trace_event.description().empty()) {
    event.AppendText(layout::Intent::kTraceDescription,
                     trace_event.description());
  }
  if (trace_event.data_size() > 0) {
    for (const auto& data : trace_event.data()) {
      Format(env, data, event);
    }
  }
}

}  // namespace grpc_zviz
