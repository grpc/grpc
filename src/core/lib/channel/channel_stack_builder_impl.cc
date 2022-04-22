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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/channel_stack_builder_impl.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<grpc_channel_stack>>
ChannelStackBuilderImpl::Build() {
  auto* stack = mutable_stack();

  // create an array of filters
  std::vector<const grpc_channel_filter*> filters;
  filters.reserve(stack->size());
  for (const auto& elem : *stack) {
    filters.push_back(elem.filter);
  }

  // calculate the size of the channel stack
  size_t channel_stack_size =
      grpc_channel_stack_size(filters.data(), filters.size());

  // allocate memory
  auto* channel_stack =
      static_cast<grpc_channel_stack*>(gpr_zalloc(channel_stack_size));

  ChannelArgs final_args = channel_args();
  if (transport() != nullptr) {
    static const grpc_arg_pointer_vtable vtable = {
        // copy
        [](void* p) { return p; },
        // destroy
        [](void*) {},
        // cmp
        [](void* a, void* b) { return QsortCompare(a, b); },
    };
    final_args = final_args.Set(GRPC_ARG_TRANSPORT,
                                ChannelArgs::Pointer(transport(), &vtable));
  }

  // and initialize it
  const grpc_channel_args* c_args = final_args.ToC();
  grpc_error_handle error = grpc_channel_stack_init(
      1,
      [](void* p, grpc_error_handle) {
        auto* stk = static_cast<grpc_channel_stack*>(p);
        grpc_channel_stack_destroy(stk);
        gpr_free(stk);
      },
      channel_stack, filters.data(), filters.size(), c_args, name(),
      channel_stack);
  grpc_channel_args_destroy(c_args);

  if (error != GRPC_ERROR_NONE) {
    grpc_channel_stack_destroy(channel_stack);
    gpr_free(channel_stack);
    auto status = grpc_error_to_absl_status(error);
    GRPC_ERROR_UNREF(error);
    return status;
  }

  // run post-initialization functions
  for (size_t i = 0; i < filters.size(); i++) {
    if ((*stack)[i].post_init != nullptr) {
      (*stack)[i].post_init(channel_stack,
                            grpc_channel_stack_element(channel_stack, i));
    }
  }

  return RefCountedPtr<grpc_channel_stack>(channel_stack);
}

}  // namespace grpc_core
