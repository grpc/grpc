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

#include <grpcpp/impl/codegen/core_codegen.h>

#include <stdlib.h>

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpcpp/support/config.h>

#include "src/core/lib/profiling/timers.h"

struct grpc_byte_buffer;

namespace grpc {

const grpc_completion_queue_factory*
CoreCodegen::grpc_completion_queue_factory_lookup(
    const grpc_completion_queue_attributes* attributes) {
  return ::grpc_completion_queue_factory_lookup(attributes);
}

grpc_completion_queue* CoreCodegen::grpc_completion_queue_create(
    const grpc_completion_queue_factory* factory,
    const grpc_completion_queue_attributes* attributes, void* reserved) {
  return ::grpc_completion_queue_create(factory, attributes, reserved);
}

grpc_completion_queue* CoreCodegen::grpc_completion_queue_create_for_next(
    void* reserved) {
  return ::grpc_completion_queue_create_for_next(reserved);
}

grpc_completion_queue* CoreCodegen::grpc_completion_queue_create_for_pluck(
    void* reserved) {
  return ::grpc_completion_queue_create_for_pluck(reserved);
}

void CoreCodegen::grpc_completion_queue_shutdown(grpc_completion_queue* cq) {
  ::grpc_completion_queue_shutdown(cq);
}

void CoreCodegen::grpc_completion_queue_destroy(grpc_completion_queue* cq) {
  ::grpc_completion_queue_destroy(cq);
}

grpc_event CoreCodegen::grpc_completion_queue_pluck(grpc_completion_queue* cq,
                                                    void* tag,
                                                    gpr_timespec deadline,
                                                    void* reserved) {
  return ::grpc_completion_queue_pluck(cq, tag, deadline, reserved);
}

void* CoreCodegen::gpr_malloc(size_t size) { return ::gpr_malloc(size); }

void CoreCodegen::gpr_free(void* p) { return ::gpr_free(p); }

void CoreCodegen::grpc_init() { ::grpc_init(); }
void CoreCodegen::grpc_shutdown() { ::grpc_shutdown(); }

void CoreCodegen::gpr_mu_init(gpr_mu* mu) { ::gpr_mu_init(mu); }
void CoreCodegen::gpr_mu_destroy(gpr_mu* mu) { ::gpr_mu_destroy(mu); }
void CoreCodegen::gpr_mu_lock(gpr_mu* mu) { ::gpr_mu_lock(mu); }
void CoreCodegen::gpr_mu_unlock(gpr_mu* mu) { ::gpr_mu_unlock(mu); }
void CoreCodegen::gpr_cv_init(gpr_cv* cv) { ::gpr_cv_init(cv); }
void CoreCodegen::gpr_cv_destroy(gpr_cv* cv) { ::gpr_cv_destroy(cv); }
int CoreCodegen::gpr_cv_wait(gpr_cv* cv, gpr_mu* mu,
                             gpr_timespec abs_deadline) {
  return ::gpr_cv_wait(cv, mu, abs_deadline);
}
void CoreCodegen::gpr_cv_signal(gpr_cv* cv) { ::gpr_cv_signal(cv); }
void CoreCodegen::gpr_cv_broadcast(gpr_cv* cv) { ::gpr_cv_broadcast(cv); }

grpc_byte_buffer* CoreCodegen::grpc_byte_buffer_copy(grpc_byte_buffer* bb) {
  return ::grpc_byte_buffer_copy(bb);
}

void CoreCodegen::grpc_byte_buffer_destroy(grpc_byte_buffer* bb) {
  ::grpc_byte_buffer_destroy(bb);
}

size_t CoreCodegen::grpc_byte_buffer_length(grpc_byte_buffer* bb) {
  return ::grpc_byte_buffer_length(bb);
}

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
void CoreCodegen::grpc_call_ref(grpc_call* call) { ::grpc_call_ref(call); }
void CoreCodegen::grpc_call_unref(grpc_call* call) { ::grpc_call_unref(call); }
void* CoreCodegen::grpc_call_arena_alloc(grpc_call* call, size_t length) {
  return ::grpc_call_arena_alloc(call, length);
}
const char* CoreCodegen::grpc_call_error_to_string(grpc_call_error error) {
  return ::grpc_call_error_to_string(error);
}

int CoreCodegen::grpc_byte_buffer_reader_init(grpc_byte_buffer_reader* reader,
                                              grpc_byte_buffer* buffer) {
  return ::grpc_byte_buffer_reader_init(reader, buffer);
}

void CoreCodegen::grpc_byte_buffer_reader_destroy(
    grpc_byte_buffer_reader* reader) {
  ::grpc_byte_buffer_reader_destroy(reader);
}

int CoreCodegen::grpc_byte_buffer_reader_next(grpc_byte_buffer_reader* reader,
                                              grpc_slice* slice) {
  return ::grpc_byte_buffer_reader_next(reader, slice);
}

int CoreCodegen::grpc_byte_buffer_reader_peek(grpc_byte_buffer_reader* reader,
                                              grpc_slice** slice) {
  return ::grpc_byte_buffer_reader_peek(reader, slice);
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
