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

#ifndef GRPCXX_IMPL_CODEGEN_GRPC_LIBRARY_H
#define GRPCXX_IMPL_CODEGEN_GRPC_LIBRARY_H

#include <grpc++/impl/codegen/core_codegen_interface.h>

namespace grpc {

class GrpcLibraryInterface {
 public:
  virtual void init() = 0;
  virtual void shutdown() = 0;
};

/// Initialized by \a grpc::GrpcLibraryInitializer from
/// <grpc++/impl/grpc_library.h>
extern GrpcLibraryInterface* g_glip;

/// Classes that require gRPC to be initialized should inherit from this class.
class GrpcLibraryCodegen {
 public:
  GrpcLibraryCodegen(bool call_grpc_init = true) : grpc_init_called_(false) {
    if (call_grpc_init) {
      GPR_CODEGEN_ASSERT(g_glip &&
                         "gRPC library not initialized. See "
                         "grpc::internal::GrpcLibraryInitializer.");
      g_glip->init();
      grpc_init_called_ = true;
    }
  }
  virtual ~GrpcLibraryCodegen() {
    if (grpc_init_called_) {
      GPR_CODEGEN_ASSERT(g_glip &&
                         "gRPC library not initialized. See "
                         "grpc::internal::GrpcLibraryInitializer.");
      g_glip->shutdown();
    }
  }

 private:
  bool grpc_init_called_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_GRPC_LIBRARY_H
