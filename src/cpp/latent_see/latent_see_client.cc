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

#include "src/core/channelz/property_list.h"
#include "src/core/util/time.h"
#include "absl/log/log.h"

namespace grpc {

using grpc_core::channelz::PropertyList;

namespace {
PropertyList ToPropertyList(const channelz::v2::PropertyList& proto) {
  PropertyList property_list;
  for (const auto& element : proto.properties()) {
    switch (element.value().kind_case()) {
      case channelz::v2::PropertyValue::kStringValue:
        property_list.Set(element.key(), element.value().string_value());
        break;
      case channelz::v2::PropertyValue::kInt64Value:
        property_list.Set(element.key(), element.value().int64_value());
        break;
      case channelz::v2::PropertyValue::kDoubleValue:
        property_list.Set(element.key(), element.value().double_value());
        break;
      case channelz::v2::PropertyValue::kBoolValue:
        property_list.Set(element.key(), element.value().bool_value());
        break;
      case channelz::v2::PropertyValue::kUint64Value:
        property_list.Set(element.key(), element.value().uint64_value());
        break;
      case channelz::v2::PropertyValue::kTimestampValue:
        gpr_timespec t;
        t.tv_sec = element.value().timestamp_value().seconds();
        t.tv_nsec = element.value().timestamp_value().nanos();
        property_list.Set(element.key(),
                          grpc_core::Timestamp::FromTimespecRoundUp(t));
        break;
      case channelz::v2::PropertyValue::kDurationValue:
        property_list.Set(element.key(),
                          grpc_core::Duration::FromSecondsAndNanoseconds(
                              element.value().duration_value().seconds(),
                              element.value().duration_value().nanos()));
        break;
      case channelz::v2::PropertyValue::kAnyValue: {
        const auto& any_value = element.value().any_value();
        if (any_value.Is<channelz::v2::PropertyList>()) {
          channelz::v2::PropertyList property_list_proto;
          any_value.UnpackTo(&property_list_proto);
          // Create a new property list from the new proto and add the new
          // property list into the existing one.
          property_list.Set(element.key(), ToPropertyList(property_list_proto));
        } else {
          // Latent-see emits only a subset of types here, and this
          // implementation handles only those types.
          // If leveraging this code elsewhere, we'll need to ensure the set of
          // types handled is expanded appropriately.
          LOG(WARNING) << "Unsupported any value type: "
                       << any_value.type_url();
        }
      } break;
      case channelz::v2::PropertyValue::KIND_NOT_SET:
      case grpc::channelz::v2::PropertyValue::kEmptyValue:
        break;
      default:
        break;
    }
  }
  return property_list;
}
}  // namespace

void ProcessLatentSeeTrace(const channelz::v2::LatentSeeTrace& trace,
                           grpc_core::latent_see::Output* output) {
  switch (trace.kind_case()) {
    case channelz::v2::LatentSeeTrace::KIND_NOT_SET:
      return;
    case channelz::v2::LatentSeeTrace::kMark:
      if (trace.has_mark()) {
        output->Mark(trace.name(), trace.tid(), trace.timestamp_ns(),
                     ToPropertyList(trace.mark().properties()));
      } else {
        output->Mark(trace.name(), trace.tid(), trace.timestamp_ns(),
                     PropertyList());
      }
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
    ProcessLatentSeeTrace(trace, output);
  }
  output->Finish();
  return reader->Finish();
}

}  // namespace grpc
