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

#include <vector>

#include "absl/status/status.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

absl::StatusOr<RefCountedPtr<grpc_channel_stack>>
ChannelStackBuilderImpl::Build() {
  auto* stack = mutable_stack();

  // calculate the size of the channel stack
  size_t channel_stack_size =
      grpc_channel_stack_size(stack->data(), stack->size());

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
  grpc_error_handle error = grpc_channel_stack_init(
      1,
      [](void* p, grpc_error_handle) {
        auto* stk = static_cast<grpc_channel_stack*>(p);
        grpc_channel_stack_destroy(stk);
        gpr_free(stk);
      },
      channel_stack, stack->data(), stack->size(), final_args.ToC().get(),
      name(), channel_stack);

  if (!GRPC_ERROR_IS_NONE(error)) {
    grpc_channel_stack_destroy(channel_stack);
    gpr_free(channel_stack);
    auto status = grpc_error_to_absl_status(error);
    GRPC_ERROR_UNREF(error);
    return status;
  }

  // run post-initialization functions
  for (size_t i = 0; i < stack->size(); i++) {
    auto* elem = grpc_channel_stack_element(channel_stack, i);
    elem->filter->post_init_channel_elem(channel_stack, elem);
  }

  return RefCountedPtr<grpc_channel_stack>(channel_stack);
}

}  // namespace grpc_core
