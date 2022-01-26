//
//
// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_channel_stack_modifier.h"

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_init.h"

namespace grpc_core {
namespace {

void* XdsChannelStackModifierArgCopy(void* p) {
  XdsChannelStackModifier* arg = static_cast<XdsChannelStackModifier*>(p);
  return arg->Ref().release();
}

void XdsChannelStackModifierArgDestroy(void* p) {
  XdsChannelStackModifier* arg = static_cast<XdsChannelStackModifier*>(p);
  arg->Unref();
}

int XdsChannelStackModifierArgCmp(void* p, void* q) {
  return QsortCompare(p, q);
}

const grpc_arg_pointer_vtable kChannelArgVtable = {
    XdsChannelStackModifierArgCopy, XdsChannelStackModifierArgDestroy,
    XdsChannelStackModifierArgCmp};

const char* kXdsChannelStackModifierChannelArgName =
    "grpc.internal.xds_channel_stack_modifier";

}  // namespace

bool XdsChannelStackModifier::ModifyChannelStack(
    grpc_channel_stack_builder* builder) {
  // Insert the filters after the census filter if present.
  grpc_channel_stack_builder_iterator* it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  while (grpc_channel_stack_builder_move_next(it)) {
    if (grpc_channel_stack_builder_iterator_is_end(it)) break;
    const char* filter_name_at_it =
        grpc_channel_stack_builder_iterator_filter_name(it);
    if (strcmp("census_server", filter_name_at_it) == 0 ||
        strcmp("opencensus_server", filter_name_at_it) == 0) {
      break;
    }
  }
  if (grpc_channel_stack_builder_iterator_is_end(it)) {
    // No census filter found. Reset iterator to the beginning. This will result
    // in prepending the list of xDS HTTP filters to the current stack. Note
    // that this stage is run before the stage that adds the top server filter,
    // resulting in these filters being finally placed after the `server`
    // filter.
    grpc_channel_stack_builder_iterator_destroy(it);
    it = grpc_channel_stack_builder_create_iterator_at_first(builder);
  }
  GPR_ASSERT(grpc_channel_stack_builder_move_next(it));
  for (const grpc_channel_filter* filter : filters_) {
    GPR_ASSERT(grpc_channel_stack_builder_add_filter_before(it, filter, nullptr,
                                                            nullptr));
  }
  grpc_channel_stack_builder_iterator_destroy(it);
  return true;
}

grpc_arg XdsChannelStackModifier::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(kXdsChannelStackModifierChannelArgName),
      const_cast<XdsChannelStackModifier*>(this), &kChannelArgVtable);
}

RefCountedPtr<XdsChannelStackModifier>
XdsChannelStackModifier::GetFromChannelArgs(const grpc_channel_args& args) {
  XdsChannelStackModifier* config_selector_provider =
      grpc_channel_args_find_pointer<XdsChannelStackModifier>(
          &args, kXdsChannelStackModifierChannelArgName);
  return config_selector_provider != nullptr ? config_selector_provider->Ref()
                                             : nullptr;
}

void RegisterXdsChannelStackModifier(CoreConfiguration::Builder* builder) {
  builder->channel_init()->RegisterStage(
      GRPC_SERVER_CHANNEL, INT_MAX, [](grpc_channel_stack_builder* builder) {
        RefCountedPtr<XdsChannelStackModifier> channel_stack_modifier =
            XdsChannelStackModifier::GetFromChannelArgs(
                *grpc_channel_stack_builder_get_channel_arguments(builder));
        if (channel_stack_modifier != nullptr) {
          return channel_stack_modifier->ModifyChannelStack(builder);
        }
        return true;
      });
}

}  // namespace grpc_core
