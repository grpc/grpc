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

#include "src/cpp/latent_see/latent_see_client.h"

#include <grpc/support/time.h>

#include <chrono>

namespace grpc {

Status FetchLatentSee(channelz::v2::LatentSee::Stub* stub, double sample_time,
                      grpc_core::latent_see::Output* output) {
  channelz::v2::GetTraceRequest request;
  request.set_sample_time(sample_time);
  ClientContext context;
  context.set_deadline(
      std::chrono::system_clock::now() +
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          sample_time * std::chrono::seconds(1) + std::chrono::seconds(30)));
  auto reader = stub->GetTrace(&context, request);
  channelz::v2::LatentSeeTrace trace;
  while (reader->Read(&trace)) {
    switch (trace.kind_case()) {
      case channelz::v2::LatentSeeTrace::KIND_NOT_SET:
        continue;
      case channelz::v2::LatentSeeTrace::kMark:
        output->Mark(trace.name(), trace.tid(), trace.timestamp_ns());
        break;
      case channelz::v2::LatentSeeTrace::kFlowBegin:
        output->FlowBegin(trace.name(), trace.tid(), trace.timestamp_ns(),
                          trace.flow_begin().id());
        break;
      case channelz::v2::LatentSeeTrace::kFlowEnd:
        output->FlowEnd(trace.name(), trace.tid(), trace.timestamp_ns(),
                        trace.flow_end().id());
        break;
      case channelz::v2::LatentSeeTrace::kSpan:
        output->Span(trace.name(), trace.tid(), trace.timestamp_ns(),
                     trace.span().duration_ns());
        break;
    }
  }
  output->Finish();
  return reader->Finish();
}

}  // namespace grpc
