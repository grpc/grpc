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

#ifndef GRPCXX_IMPL_GRPC_LIBRARY_H
#define GRPCXX_IMPL_GRPC_LIBRARY_H

#include <iostream>

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/core_codegen.h>
#include <grpc++/impl/codegen/grpc_library.h>
#include <grpc/grpc.h>

namespace grpc {

namespace internal {
class GrpcLibrary GRPC_FINAL : public GrpcLibraryInterface {
 public:
  void init() GRPC_OVERRIDE { grpc_init(); }
  void shutdown() GRPC_OVERRIDE { grpc_shutdown(); }
};

static GrpcLibrary g_gli;
static CoreCodegen g_core_codegen;

/// Instantiating this class ensures the proper initialization of gRPC.
class GrpcLibraryInitializer GRPC_FINAL {
 public:
  GrpcLibraryInitializer() {
    if (grpc::g_glip == nullptr) {
      grpc::g_glip = &g_gli;
    }
    if (grpc::g_core_codegen_interface == nullptr) {
      grpc::g_core_codegen_interface = &g_core_codegen;
    }
  }

  /// A no-op method to force the linker to reference this class, which will
  /// take care of initializing and shutting down the gRPC runtime.
  int summon() { return 0; }
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPCXX_IMPL_GRPC_LIBRARY_H
