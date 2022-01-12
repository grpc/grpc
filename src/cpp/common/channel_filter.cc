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

#include "src/cpp/common/channel_filter.h"

#include <string.h>

#include <grpcpp/impl/codegen/slice.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"

namespace grpc {

// MetadataBatch

void MetadataBatch::AddMetadata(const string& key, const string& value) {
  batch_->Append(key, grpc_core::Slice::FromCopiedString(value),
                 [&](absl::string_view error, const grpc_core::Slice&) {
                   gpr_log(GPR_INFO, "%s",
                           absl::StrCat("MetadataBatch::AddMetadata error:",
                                        error, " key=", key, " value=", value)
                               .c_str());
                 });
}

// ChannelData

void ChannelData::StartTransportOp(grpc_channel_element* elem,
                                   TransportOp* op) {
  grpc_channel_next_op(elem, op->op());
}

void ChannelData::GetInfo(grpc_channel_element* elem,
                          const grpc_channel_info* channel_info) {
  grpc_channel_next_get_info(elem, channel_info);
}

// CallData

void CallData::StartTransportStreamOpBatch(grpc_call_element* elem,
                                           TransportStreamOpBatch* op) {
  grpc_call_next_op(elem, op->op());
}

void CallData::SetPollsetOrPollsetSet(grpc_call_element* elem,
                                      grpc_polling_entity* pollent) {
  grpc_call_stack_ignore_set_pollset_or_pollset_set(elem, pollent);
}

namespace internal {

void RegisterChannelFilter(
    grpc_channel_stack_type stack_type, int priority,
    std::function<bool(const grpc_channel_args&)> include_filter,
    const grpc_channel_filter* filter) {
  auto maybe_add_filter = [include_filter,
                           filter](grpc_channel_stack_builder* builder) {
    if (include_filter != nullptr) {
      const grpc_channel_args* args =
          grpc_channel_stack_builder_get_channel_arguments(builder);
      if (!include_filter(*args)) return true;
    }
    return grpc_channel_stack_builder_prepend_filter(builder, filter, nullptr,
                                                     nullptr);
  };
  grpc_core::CoreConfiguration::RegisterBuilder(
      [stack_type, priority,
       maybe_add_filter](grpc_core::CoreConfiguration::Builder* builder) {
        builder->channel_init()->RegisterStage(stack_type, priority,
                                               maybe_add_filter);
      });
}

}  // namespace internal

}  // namespace grpc
