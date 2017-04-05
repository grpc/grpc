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

#include <grpc++/impl/codegen/core_codegen.h>

#include <stdlib.h>

#include <grpc++/support/config.h>
#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

#include "src/core/lib/profiling/timers.h"

extern "C" {
struct grpc_byte_buffer;
}

namespace grpc {

grpc_completion_queue* CoreCodegen::grpc_completion_queue_create(
    void* reserved) {
  return ::grpc_completion_queue_create(reserved);
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

void CoreCodegen::gpr_mu_init(gpr_mu* mu) { ::gpr_mu_init(mu); };
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

void CoreCodegen::grpc_byte_buffer_destroy(grpc_byte_buffer* bb) {
  ::grpc_byte_buffer_destroy(bb);
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

grpc_byte_buffer* CoreCodegen::grpc_raw_byte_buffer_create(grpc_slice* slice,
                                                           size_t nslices) {
  return ::grpc_raw_byte_buffer_create(slice, nslices);
}

grpc_slice CoreCodegen::grpc_empty_slice() { return ::grpc_empty_slice(); }

grpc_slice CoreCodegen::grpc_slice_malloc(size_t length) {
  return ::grpc_slice_malloc(length);
}

void CoreCodegen::grpc_slice_unref(grpc_slice slice) {
  ::grpc_slice_unref(slice);
}

grpc_slice CoreCodegen::grpc_slice_split_tail(grpc_slice* s, size_t split) {
  return ::grpc_slice_split_tail(s, split);
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
