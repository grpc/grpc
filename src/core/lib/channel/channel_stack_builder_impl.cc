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

namespace grpc_core {

grpc_error_handle ChannelStackBuilderImpl::Build(size_t prefix_bytes,
                                                 int initial_refs,
                                                 grpc_iomgr_cb_func destroy,
                                                 void* destroy_arg,
                                                 void** result) {
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

  // allocate memory, with prefix_bytes followed by channel_stack_size
  *result = gpr_zalloc(prefix_bytes + channel_stack_size);
  // fetch a pointer to the channel stack
  grpc_channel_stack* channel_stack = reinterpret_cast<grpc_channel_stack*>(
      static_cast<char*>(*result) + prefix_bytes);

  const grpc_channel_args* final_args;
  if (transport() != nullptr) {
    static const grpc_arg_pointer_vtable vtable = {
        // copy
        [](void* p) { return p; },
        // destroy
        [](void*) {},
        // cmp
        [](void* a, void* b) { return QsortCompare(a, b); },
    };
    grpc_arg arg = grpc_channel_arg_pointer_create(
        const_cast<char*>(GRPC_ARG_TRANSPORT), transport(), &vtable);
    final_args = grpc_channel_args_copy_and_add(channel_args(), &arg, 1);
  } else {
    final_args = channel_args();
  }

  // and initialize it
  grpc_error_handle error = grpc_channel_stack_init(
      initial_refs, destroy, destroy_arg == nullptr ? *result : destroy_arg,
      filters.data(), filters.size(), final_args, name(), channel_stack);

  if (final_args != channel_args()) {
    grpc_channel_args_destroy(final_args);
  }

  if (error != GRPC_ERROR_NONE) {
    grpc_channel_stack_destroy(channel_stack);
    gpr_free(*result);
    *result = nullptr;
    return error;
  }

  // run post-initialization functions
  for (size_t i = 0; i < filters.size(); i++) {
    if ((*stack)[i].post_init != nullptr) {
      (*stack)[i].post_init(channel_stack,
                            grpc_channel_stack_element(channel_stack, i));
    }
  }

  return GRPC_ERROR_NONE;
}

}  // namespace grpc_core
