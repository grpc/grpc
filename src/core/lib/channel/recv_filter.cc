/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/channel/recv_filter.h"

typedef struct recv_call_data {
  grpc_transport_stream_recv_op_batch_func recv_func;
  void* recv_func_arg;
} call_data;

static grpc_error* recv_init_channel_elem(grpc_channel_element* elem,
                                          grpc_channel_element_args* args) {
  GPR_ASSERT(args->is_first);
  return GRPC_ERROR_NONE;
}

static void recv_destroy_channel_elem(grpc_channel_element* elem) {}

static grpc_error* recv_init_call_elem(grpc_call_element* elem,
                                       const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->recv_func = args->recv_func;
  calld->recv_func_arg = args->recv_func_arg;
  return GRPC_ERROR_NONE;
}

static void recv_destroy_call_elem(grpc_call_element* elem,
                                   const grpc_call_final_info* final_info,
                                   grpc_closure* then_schedule_closure) {}

static void recv_start_transport_stream_recv_op_batch(
    grpc_call_element* elem, grpc_transport_stream_recv_op_batch* batch,
    grpc_error* error) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->recv_func(batch, calld->recv_func_arg, error);
}

const grpc_channel_filter grpc_recv_filter = {
    grpc_call_next_op,
    recv_start_transport_stream_recv_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    recv_init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    recv_destroy_call_elem,
    0,
    recv_init_channel_elem,
    recv_destroy_channel_elem,
    grpc_channel_next_get_info,
    "recv",
};
