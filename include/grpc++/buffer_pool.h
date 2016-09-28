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

#ifndef GRPCXX_BUFFER_POOL_H
#define GRPCXX_BUFFER_POOL_H

struct grpc_buffer_pool;

#include <grpc++/impl/codegen/config.h>

namespace grpc {

/// BufferPool represents a bound on memory usage by the gRPC library.
/// A BufferPool can be attached to a server (via ServerBuilder), or a client
/// channel (via ChannelArguments). gRPC will attempt to keep memory used by
/// all attached entities below the BufferPool bound.
class BufferPool GRPC_FINAL {
 public:
  explicit BufferPool(const grpc::string& name);
  BufferPool();
  ~BufferPool();

  /// Resize this BufferPool to a new size. If new_size is smaller than the
  /// current size of the pool, memory usage will be monotonically decreased
  /// until it falls under new_size. No time bound is given for this to occur
  /// however.
  BufferPool& Resize(size_t new_size);

  grpc_buffer_pool* c_buffer_pool() const { return impl_; }

 private:
  BufferPool(const BufferPool& rhs);
  BufferPool& operator=(const BufferPool& rhs);

  grpc_buffer_pool* const impl_;
};

}  // namespace grpc

#endif
