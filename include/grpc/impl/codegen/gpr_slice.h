/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef GRPC_IMPL_CODEGEN_GPR_SLICE_H
#define GRPC_IMPL_CODEGEN_GPR_SLICE_H

/* WARNING: Please do not use this header. This was added as a temporary measure
 * to not break some of the external projects that depend on gpr_slice_*
 * functions. We are actively working on moving all the gpr_slice_* references
 * to grpc_slice_* and this file will be removed
 * */

/* TODO (sreek) - Allowed by default but will be very soon turned off */
#define GRPC_ALLOW_GPR_SLICE_FUNCTIONS 1

#ifdef GRPC_ALLOW_GPR_SLICE_FUNCTIONS

#define gpr_slice_refcount grpc_slice_refcount
#define gpr_slice grpc_slice
#define gpr_slice_buffer grpc_slice_buffer

#define gpr_slice_ref grpc_slice_ref
#define gpr_slice_unref grpc_slice_unref
#define gpr_slice_new grpc_slice_new
#define gpr_slice_new_with_user_data grpc_slice_new_with_user_data
#define gpr_slice_new_with_len grpc_slice_new_with_len
#define gpr_slice_malloc grpc_slice_malloc
#define gpr_slice_from_copied_string grpc_slice_from_copied_string
#define gpr_slice_from_copied_buffer grpc_slice_from_copied_buffer
#define gpr_slice_from_static_string grpc_slice_from_static_string
#define gpr_slice_sub grpc_slice_sub
#define gpr_slice_sub_no_ref grpc_slice_sub_no_ref
#define gpr_slice_split_tail grpc_slice_split_tail
#define gpr_slice_split_head grpc_slice_split_head
#define gpr_slice_cmp grpc_slice_cmp
#define gpr_slice_str_cmp grpc_slice_str_cmp

#define gpr_slice_buffer grpc_slice_buffer
#define gpr_slice_buffer_init grpc_slice_buffer_init
#define gpr_slice_buffer_destroy grpc_slice_buffer_destroy
#define gpr_slice_buffer_add grpc_slice_buffer_add
#define gpr_slice_buffer_add_indexed grpc_slice_buffer_add_indexed
#define gpr_slice_buffer_addn grpc_slice_buffer_addn
#define gpr_slice_buffer_tiny_add grpc_slice_buffer_tiny_add
#define gpr_slice_buffer_pop grpc_slice_buffer_pop
#define gpr_slice_buffer_reset_and_unref grpc_slice_buffer_reset_and_unref
#define gpr_slice_buffer_swap grpc_slice_buffer_swap
#define gpr_slice_buffer_move_into grpc_slice_buffer_move_into
#define gpr_slice_buffer_trim_end grpc_slice_buffer_trim_end
#define gpr_slice_buffer_move_first grpc_slice_buffer_move_first
#define gpr_slice_buffer_take_first grpc_slice_buffer_take_first

#endif /* GRPC_ALLOW_GPR_SLICE_FUNCTIONS */

#endif /* GRPC_IMPL_CODEGEN_GPR_SLICE_H */
