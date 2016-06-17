/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPCXX_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H
#define GRPCXX_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc/impl/codegen/grpc_types.h>

namespace grpc {

/// Interface between the codegen library and the minimal subset of core
/// features required by the generated code.
///
/// All undocumented methods are simply forwarding the call to their namesakes.
/// Please refer to their corresponding documentation for details.
///
/// \warning This interface should be considered internal and private.
class CoreCodegenInterface {
 public:
  /// Upon a failed assertion, log the error.
  virtual void assert_fail(const char* failed_assertion) = 0;

  virtual grpc_completion_queue* grpc_completion_queue_create(
      void* reserved) = 0;
  virtual void grpc_completion_queue_destroy(grpc_completion_queue* cq) = 0;
  virtual grpc_event grpc_completion_queue_pluck(grpc_completion_queue* cq,
                                                 void* tag,
                                                 gpr_timespec deadline,
                                                 void* reserved) = 0;

  virtual void* gpr_malloc(size_t size) = 0;
  virtual void gpr_free(void* p) = 0;

  virtual void grpc_byte_buffer_destroy(grpc_byte_buffer* bb) = 0;

  virtual void grpc_byte_buffer_reader_init(grpc_byte_buffer_reader* reader,
                                            grpc_byte_buffer* buffer) = 0;
  virtual void grpc_byte_buffer_reader_destroy(
      grpc_byte_buffer_reader* reader) = 0;
  virtual int grpc_byte_buffer_reader_next(grpc_byte_buffer_reader* reader,
                                           gpr_slice* slice) = 0;

  virtual grpc_byte_buffer* grpc_raw_byte_buffer_create(gpr_slice* slice,
                                                        size_t nslices) = 0;

  virtual gpr_slice gpr_slice_malloc(size_t length) = 0;
  virtual void gpr_slice_unref(gpr_slice slice) = 0;
  virtual gpr_slice gpr_slice_split_tail(gpr_slice* s, size_t split) = 0;
  virtual void gpr_slice_buffer_add(gpr_slice_buffer* sb, gpr_slice slice) = 0;
  virtual void gpr_slice_buffer_pop(gpr_slice_buffer* sb) = 0;

  virtual void grpc_metadata_array_init(grpc_metadata_array* array) = 0;
  virtual void grpc_metadata_array_destroy(grpc_metadata_array* array) = 0;

  virtual const Status& ok() = 0;
  virtual const Status& cancelled() = 0;

  virtual gpr_timespec gpr_inf_future(gpr_clock_type type) = 0;
};

extern CoreCodegenInterface* g_core_codegen_interface;

/// Codegen specific version of \a GPR_ASSERT.
#define GPR_CODEGEN_ASSERT(x)                          \
  do {                                                 \
    if (!(x)) {                                        \
      grpc::g_core_codegen_interface->assert_fail(#x); \
    }                                                  \
  } while (0)

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H
