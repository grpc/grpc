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

#ifndef GRPCXX_IMPL_CALL_H
#define GRPCXX_IMPL_CALL_H

#include <grpc/grpc.h>
#include <grpc++/completion_queue.h>
#include <grpc++/config.h>
#include <grpc++/status.h>

#include <memory>
#include <map>

struct grpc_call;
struct grpc_op;

namespace grpc {

class ByteBuffer;
class Call;

class CallNoOp {
 protected:
  void AddOp(grpc_op* ops, size_t* nops) {}
  void FinishOp(void* tag, bool* status) {}
};

class CallOpSendInitialMetadata {
 public:
  void SendInitialMetadata(const std::multimap<grpc::string, grpc::string>& metadata);

 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

class CallOpSendMessage {
 public:
  template <class M>
  void SendMessage(const M& message);

 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

template <class M>
class CallOpRecvMessage {
 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

class CallOpGenericRecvMessage {
 public:
  template <class R>
  void RecvMessage(R* message);

 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

class CallOpClientSendClose {
 public:
  void ClientSendClose();

 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

class CallOpServerSendStatus {
 public:
  void ServerSendStatus(const std::multimap<grpc::string, grpc::string>& trailing_metadata, const Status& status);

 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

class CallOpRecvInitialMetadata {
 public:
  void RecvInitialMetadata(ClientContext* context);

 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

class CallOpClientRecvStatus {
 public:
  void ClientRecvStatus(ClientContext* context, Status* status);

 protected:
  void AddOp(grpc_op* ops, size_t* nops);
  void FinishOp(void* tag, bool* status);
};

class CallOpSetInterface : public CompletionQueueTag {
 public:
  virtual void FillOps(grpc_op* ops, size_t* nops) = 0;
};

template <class T, int I>
class WrapAndDerive : public T {};

template <class Op1 = CallNoOp, class Op2 = CallNoOp, class Op3 = CallNoOp, class Op4 = CallNoOp, class Op5 = CallNoOp, class Op6 = CallNoOp>
class CallOpSet : public CallOpSetInterface, 
public WrapAndDerive<Op1, 1>, 
public WrapAndDerive<Op2, 2>, 
public WrapAndDerive<Op3, 3>, 
public WrapAndDerive<Op4, 4>, 
public WrapAndDerive<Op5, 5>, 
public WrapAndDerive<Op6, 6> {
 public:
  void FillOps(grpc_op* ops, size_t* nops) GRPC_OVERRIDE {
    this->Op1::AddOp(ops, nops);
    this->Op2::AddOp(ops, nops);
    this->Op3::AddOp(ops, nops);
    this->Op4::AddOp(ops, nops);
    this->Op5::AddOp(ops, nops);
    this->Op6::AddOp(ops, nops);
  }

  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
    this->Op1::FinishOp(*tag, status);
    this->Op2::FinishOp(*tag, status);
    this->Op3::FinishOp(*tag, status);
    this->Op4::FinishOp(*tag, status);
    this->Op5::FinishOp(*tag, status);
    this->Op6::FinishOp(*tag, status);
    *tag = return_tag_;
    return true;
  }

  void SetOutputTag(void* return_tag) { return_tag_ = return_tag; }

 private:
  void *return_tag_;
};

#if 0
class CallOpBuffer : public CompletionQueueTag {
 public:
  CallOpBuffer();
  ~CallOpBuffer();

  void Reset(void* next_return_tag);

  // Does not take ownership.
  void AddSendInitialMetadata(
      std::multimap<grpc::string, grpc::string>* metadata);
  void AddSendInitialMetadata(ClientContext* ctx);
  void AddRecvInitialMetadata(ClientContext* ctx);
  void AddSendMessage(const grpc::protobuf::Message& message);
  void AddSendMessage(const ByteBuffer& message);
  void AddRecvMessage(grpc::protobuf::Message* message);
  void AddRecvMessage(ByteBuffer* message);
  void AddClientSendClose();
  void AddClientRecvStatus(ClientContext* ctx, Status* status);
  void AddServerSendStatus(std::multimap<grpc::string, grpc::string>* metadata,
                           const Status& status);
  void AddServerRecvClose(bool* cancelled);

  // INTERNAL API:

  // Convert to an array of grpc_op elements
  void FillOps(grpc_op* ops, size_t* nops);

  // Called by completion queue just prior to returning from Next() or Pluck()
  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE;

  void set_max_message_size(int max_message_size) {
    max_message_size_ = max_message_size;
  }

  bool got_message;

 private:
  void* return_tag_;
  // Send initial metadata
  bool send_initial_metadata_;
  size_t initial_metadata_count_;
  grpc_metadata* initial_metadata_;
  // Recv initial metadta
  std::multimap<grpc::string, grpc::string>* recv_initial_metadata_;
  grpc_metadata_array recv_initial_metadata_arr_;
  // Send message
  const grpc::protobuf::Message* send_message_;
  const ByteBuffer* send_message_buffer_;
  grpc_byte_buffer* send_buf_;
  // Recv message
  grpc::protobuf::Message* recv_message_;
  ByteBuffer* recv_message_buffer_;
  grpc_byte_buffer* recv_buf_;
  int max_message_size_;
  // Client send close
  bool client_send_close_;
  // Client recv status
  std::multimap<grpc::string, grpc::string>* recv_trailing_metadata_;
  Status* recv_status_;
  grpc_metadata_array recv_trailing_metadata_arr_;
  grpc_status_code status_code_;
  char* status_details_;
  size_t status_details_capacity_;
  // Server send status
  bool send_status_available_;
  grpc_status_code send_status_code_;
  grpc::string send_status_details_;
  size_t trailing_metadata_count_;
  grpc_metadata* trailing_metadata_;
  int cancelled_buf_;
  bool* recv_closed_;
};
#endif

// SneakyCallOpBuffer does not post completions to the completion queue
template <class Op1 = CallNoOp, class Op2 = CallNoOp, class Op3 = CallNoOp, class Op4 = CallNoOp, class Op5 = CallNoOp, class Op6 = CallNoOp>
class SneakyCallOpSet GRPC_FINAL : public CallOpSet<Op1, Op2, Op3, Op4, Op5, Op6> {
 public:
  bool FinalizeResult(void** tag, bool* status) GRPC_OVERRIDE {
    return CallOpSet<Op1, Op2, Op3, Op4, Op5, Op6>::FinalizeResult(tag, status) && false;
  }
};

// Channel and Server implement this to allow them to hook performing ops
class CallHook {
 public:
  virtual ~CallHook() {}
  virtual void PerformOpsOnCall(CallOpSetInterface* ops, Call* call) = 0;
};

// Straightforward wrapping of the C call object
class Call GRPC_FINAL {
 public:
  /* call is owned by the caller */
  Call(grpc_call* call, CallHook* call_hook_, CompletionQueue* cq);
  Call(grpc_call* call, CallHook* call_hook_, CompletionQueue* cq,
       int max_message_size);

  void PerformOps(CallOpSetInterface* ops);

  grpc_call* call() { return call_; }
  CompletionQueue* cq() { return cq_; }

  int max_message_size() { return max_message_size_; }

 private:
  CallHook* call_hook_;
  CompletionQueue* cq_;
  grpc_call* call_;
  int max_message_size_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CALL_H
