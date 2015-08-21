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

#ifndef GRPCXX_SUPPORT_SLICE_H
#define GRPCXX_SUPPORT_SLICE_H

#include <grpc/support/slice.h>
#include <grpc++/support/config.h>

namespace grpc {

class Slice GRPC_FINAL {
 public:
  // construct empty slice
  Slice();
  // destructor - drops one ref
  ~Slice();
  // construct slice from grpc slice, adding a ref
  enum AddRef { ADD_REF };
  Slice(gpr_slice slice, AddRef);
  // construct slice from grpc slice, stealing a ref
  enum StealRef { STEAL_REF };
  Slice(gpr_slice slice, StealRef);
  // copy constructor - adds a ref
  Slice(const Slice& other);
  // assignment - ref count is unchanged
  Slice& operator=(Slice other) {
    std::swap(slice_, other.slice_);
    return *this;
  }

  size_t size() const { return GPR_SLICE_LENGTH(slice_); }
  const gpr_uint8* begin() const { return GPR_SLICE_START_PTR(slice_); }
  const gpr_uint8* end() const { return GPR_SLICE_END_PTR(slice_); }

 private:
  friend class ByteBuffer;

  gpr_slice slice_;
};

}  // namespace grpc

#endif  // GRPCXX_SUPPORT_SLICE_H
