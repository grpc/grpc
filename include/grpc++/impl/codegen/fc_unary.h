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

#ifndef GRPCXX_IMPL_CODEGEN_FC_UNARY_H
#define GRPCXX_IMPL_CODEGEN_FC_UNARY_H

#include <grpc++/impl/codegen/call.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/server_context.h>

namespace grpc {

/// A class to represent a flow-controlled unary call. This is something
/// of a hybrid between conventional unary and streaming. This is invoked
/// through a unary call on the client side, but the server responds to it
/// as though it were a single-ping-pong streaming call. The server can use
/// the \a Size method to determine an upper-bound on the size of the message
/// A key difference relative to streaming: an FCUnary must have exactly 1 Read
/// and exactly 1 Write, in that order, to function correctly.
/// Otherwise, the RPC is in error
template <class RequestType, class ResponseType>
  class FCUnary GRPC_FINAL {
 public:
 FCUnary(Call* call, ServerContext* ctx, int max_message_size): call_(call), ctx_(ctx), max_msg_size_(max_message_size), read_done_(false), write_done_(false) {}
  ~FCUnary() {}
  uint32_t Size() {return max_msg_size_;}
  bool Read(RequestType *request) {
    if (read_done_) {
      return false;      
    }
    read_done_ = true;
    CallOpSet<CallOpRecvMessage<RequestType>> ops;
    ops.RecvMessage(request);
    call_->PerformOps(&ops);
    return call_->cq()->Pluck(&ops) && ops.got_message;
  }
  bool Write(const ResponseType& response) {return Write(response, WriteOptions());}
  bool Write(const ResponseType& response, const WriteOptions& options) {
    if (write_done_ || !read_done_) {
      return false;      
    }
    write_done_ = true;
    CallOpSet<CallOpSendInitialMetadata, CallOpSendMessage> ops;
    if (!ops.SendMessage(response, options).ok()) {
      return false;
    }
    if (!ctx_->sent_initial_metadata_) {
      ops.SendInitialMetadata(ctx_->initial_metadata_,
                              ctx_->initial_metadata_flags());
      ctx_->sent_initial_metadata_ = true;
    } else {
      return false;
    }
    call_->PerformOps(&ops);
    return call_->cq()->Pluck(&ops);    
  }
 private:
  Call* const call_;
  ServerContext* const ctx_;
  const int max_msg_size_;
  bool read_done_;
  bool write_done_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_FC_UNARY_H
