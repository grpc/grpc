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
#include <grpc/impl/codegen/alloc.h>
#include <grpc/impl/codegen/byte_buffer.h>
#include <grpc/impl/codegen/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>

#include "src/core/lib/profiling/timers.h"

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

void CoreCodegen::grpc_byte_buffer_destroy(grpc_byte_buffer* bb) {
  ::grpc_byte_buffer_destroy(bb);
}

void CoreCodegen::grpc_byte_buffer_reader_init(grpc_byte_buffer_reader* reader,
                                               grpc_byte_buffer* buffer) {
  ::grpc_byte_buffer_reader_init(reader, buffer);
}

void CoreCodegen::grpc_byte_buffer_reader_destroy(
    grpc_byte_buffer_reader* reader) {
  ::grpc_byte_buffer_reader_destroy(reader);
}

int CoreCodegen::grpc_byte_buffer_reader_next(grpc_byte_buffer_reader* reader,
                                              gpr_slice* slice) {
  return ::grpc_byte_buffer_reader_next(reader, slice);
}

grpc_byte_buffer* CoreCodegen::grpc_raw_byte_buffer_create(gpr_slice* slice,
                                                           size_t nslices) {
  return ::grpc_raw_byte_buffer_create(slice, nslices);
}

gpr_slice CoreCodegen::gpr_slice_malloc(size_t length) {
  return ::gpr_slice_malloc(length);
}

void CoreCodegen::gpr_slice_unref(gpr_slice slice) { ::gpr_slice_unref(slice); }

gpr_slice CoreCodegen::gpr_slice_split_tail(gpr_slice* s, size_t split) {
  return ::gpr_slice_split_tail(s, split);
}

void CoreCodegen::gpr_slice_buffer_add(gpr_slice_buffer* sb, gpr_slice slice) {
  ::gpr_slice_buffer_add(sb, slice);
}

void CoreCodegen::gpr_slice_buffer_pop(gpr_slice_buffer* sb) {
  ::gpr_slice_buffer_pop(sb);
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

void CoreCodegen::assert_fail(const char* failed_assertion) {
  gpr_log(GPR_ERROR, "assertion failed: %s", failed_assertion);
  abort();
}

}  // namespace grpc
