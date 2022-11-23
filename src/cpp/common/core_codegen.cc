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

#include <stdlib.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpcpp/impl/codegen/core_codegen.h>
#include <grpcpp/support/status.h>

namespace grpc {

grpc_call_error CoreCodegen::grpc_call_start_batch(grpc_call* call,
                                                   const grpc_op* ops,
                                                   size_t nops, void* tag,
                                                   void* reserved) {
  return ::grpc_call_start_batch(call, ops, nops, tag, reserved);
}

grpc_call_error CoreCodegen::grpc_call_cancel_with_status(
    grpc_call* call, grpc_status_code status, const char* description,
    void* reserved) {
  return ::grpc_call_cancel_with_status(call, status, description, reserved);
}

int CoreCodegen::grpc_call_failed_before_recv_message(const grpc_call* c) {
  return ::grpc_call_failed_before_recv_message(c);
}
void CoreCodegen::grpc_call_ref(grpc_call* call) { ::grpc_call_ref(call); }
void CoreCodegen::grpc_call_unref(grpc_call* call) { ::grpc_call_unref(call); }
void* CoreCodegen::grpc_call_arena_alloc(grpc_call* call, size_t length) {
  return ::grpc_call_arena_alloc(call, length);
}
const char* CoreCodegen::grpc_call_error_to_string(grpc_call_error error) {
  return ::grpc_call_error_to_string(error);
}

grpc_byte_buffer* CoreCodegen::grpc_raw_byte_buffer_create(grpc_slice* slice,
                                                           size_t nslices) {
  return ::grpc_raw_byte_buffer_create(slice, nslices);
}

grpc_slice CoreCodegen::grpc_slice_new_with_user_data(void* p, size_t len,
                                                      void (*destroy)(void*),
                                                      void* user_data) {
  return ::grpc_slice_new_with_user_data(p, len, destroy, user_data);
}

grpc_slice CoreCodegen::grpc_slice_new_with_len(void* p, size_t len,
                                                void (*destroy)(void*,
                                                                size_t)) {
  return ::grpc_slice_new_with_len(p, len, destroy);
}

grpc_slice CoreCodegen::grpc_empty_slice() { return ::grpc_empty_slice(); }

grpc_slice CoreCodegen::grpc_slice_malloc(size_t length) {
  return ::grpc_slice_malloc(length);
}

void CoreCodegen::grpc_slice_unref(grpc_slice slice) {
  ::grpc_slice_unref(slice);
}

grpc_slice CoreCodegen::grpc_slice_ref(grpc_slice slice) {
  return ::grpc_slice_ref(slice);
}

grpc_slice CoreCodegen::grpc_slice_split_tail(grpc_slice* s, size_t split) {
  return ::grpc_slice_split_tail(s, split);
}

grpc_slice CoreCodegen::grpc_slice_split_head(grpc_slice* s, size_t split) {
  return ::grpc_slice_split_head(s, split);
}

grpc_slice CoreCodegen::grpc_slice_sub(grpc_slice s, size_t begin, size_t end) {
  return ::grpc_slice_sub(s, begin, end);
}

grpc_slice CoreCodegen::grpc_slice_from_static_buffer(const void* buffer,
                                                      size_t length) {
  return ::grpc_slice_from_static_buffer(buffer, length);
}

grpc_slice CoreCodegen::grpc_slice_from_copied_buffer(const void* buffer,
                                                      size_t length) {
  return ::grpc_slice_from_copied_buffer(static_cast<const char*>(buffer),
                                         length);
}

void CoreCodegen::grpc_slice_buffer_add(grpc_slice_buffer* sb,
                                        grpc_slice slice) {
  ::grpc_slice_buffer_add(sb, slice);
}

void CoreCodegen::grpc_slice_buffer_add_indexed(grpc_slice_buffer* sb,
                                                grpc_slice slice) {
  ::grpc_slice_buffer_add_indexed(sb, slice);
}

void CoreCodegen::grpc_slice_buffer_pop(grpc_slice_buffer* sb) {
  ::grpc_slice_buffer_pop(sb);
}

void CoreCodegen::grpc_metadata_array_init(grpc_metadata_array* array) {
  ::grpc_metadata_array_init(array);
}

void CoreCodegen::grpc_metadata_array_destroy(grpc_metadata_array* array) {
  ::grpc_metadata_array_destroy(array);
}

const Status& CoreCodegen::ok() { return grpc::Status::OK; }

const Status& CoreCodegen::cancelled() { return grpc::Status::CANCELLED; }

gpr_timespec CoreCodegen::gpr_inf_future(gpr_clock_type type) {
  return ::gpr_inf_future(type);
}

gpr_timespec CoreCodegen::gpr_time_0(gpr_clock_type type) {
  return ::gpr_time_0(type);
}

void CoreCodegen::assert_fail(const char* failed_assertion, const char* file,
                              int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "assertion failed: %s",
          failed_assertion);
  abort();
}

}  // namespace grpc
