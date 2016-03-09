/*
 *
 * Copyright 2015-2016, Google Inc.
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

/// XXX
#ifndef GRPCXX_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H
#define GRPCXX_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc++/impl/codegen/config_protobuf.h>

namespace grpc {

class CoreCodegenInterface;

extern CoreCodegenInterface* g_core_codegen_interface;

class CoreCodegenInterface {
 public:
  virtual grpc_completion_queue* grpc_completion_queue_create(
      void* reserved) = 0;
  virtual void grpc_completion_queue_destroy(grpc_completion_queue* cq) = 0;
  virtual grpc_event grpc_completion_queue_pluck(grpc_completion_queue* cq,
                                                 void* tag,
                                                 gpr_timespec deadline,
                                                 void* reserved) = 0;

  // Serialize the msg into a buffer created inside the function. The caller
  // should destroy the returned buffer when done with it. If serialization
  // fails,
  // false is returned and buffer is left unchanged.
  virtual Status SerializeProto(const grpc::protobuf::Message& msg,
                                grpc_byte_buffer** buffer) = 0;

  // The caller keeps ownership of buffer and msg.
  virtual Status DeserializeProto(grpc_byte_buffer* buffer,
                                  grpc::protobuf::Message* msg,
                                  int max_message_size) = 0;

  virtual void* gpr_malloc(size_t size) = 0;
  virtual void gpr_free(void* p) = 0;

  virtual void grpc_byte_buffer_destroy(grpc_byte_buffer* bb) = 0;
  virtual void grpc_metadata_array_init(grpc_metadata_array* array) = 0;
  virtual void grpc_metadata_array_destroy(grpc_metadata_array* array) = 0;

  virtual gpr_timespec gpr_inf_future(gpr_clock_type type) = 0;

  virtual void assert_fail(const char* failed_assertion) = 0;
};

/* XXX */
#define GPR_CODEGEN_ASSERT(x)                                            \
  do {                                                                   \
    if (!(x)) {                                                          \
      grpc::g_core_codegen_interface->assert_fail(#x);                   \
    }                                                                    \
  } while (0)

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_CORE_CODEGEN_INTERFACE_H
