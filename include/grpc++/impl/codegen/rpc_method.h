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

#ifndef GRPCXX_IMPL_CODEGEN_RPC_METHOD_H
#define GRPCXX_IMPL_CODEGEN_RPC_METHOD_H

#include <memory>

#include <grpc++/impl/codegen/channel_interface.h>

namespace grpc {

class RpcMethod {
 public:
  enum RpcType {
    NORMAL_RPC = 0,
    CLIENT_STREAMING,  // request streaming
    SERVER_STREAMING,  // response streaming
    BIDI_STREAMING
  };

  RpcMethod(const char* name, RpcType type)
      : name_(name), method_type_(type), channel_tag_(NULL) {}

  RpcMethod(const char* name, RpcType type,
            const std::shared_ptr<ChannelInterface>& channel)
      : name_(name),
        method_type_(type),
        channel_tag_(channel->RegisterMethod(name)) {}

  const char* name() const { return name_; }
  RpcType method_type() const { return method_type_; }
  void SetMethodType(RpcType type) { method_type_ = type; }
  void* channel_tag() const { return channel_tag_; }

 private:
  const char* const name_;
  RpcType method_type_;
  void* const channel_tag_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_RPC_METHOD_H
