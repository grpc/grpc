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

#ifndef GRPCXX_IMPL_CODEGEN_CALL_H
#define GRPCXX_IMPL_CODEGEN_CALL_H

#include <cstring>
#include <functional>
#include <map>
#include <memory>

#include <grpc++/impl/codegen/call_hook.h>
#include <grpc++/impl/codegen/client_context.h>
#include <grpc++/impl/codegen/completion_queue_tag.h>
#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/serialization_traits.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc++/impl/codegen/status_helper.h>
#include <grpc++/impl/codegen/string_ref.h>

#include <grpc/impl/codegen/compression_types.h>
#include <grpc/impl/codegen/grpc_types.h>

struct grpc_byte_buffer;

namespace grpc {

class ByteBuffer;
class Call;
class CallHook;
class CompletionQueue;
extern CoreCodegenInterface* g_core_codegen_interface;

inline void FillMetadataMap(
    grpc_metadata_array* arr,
    std::multimap<grpc::string_ref, grpc::string_ref>* metadata) {
  for (size_t i = 0; i < arr->count; i++) {
    // TODO(yangg) handle duplicates?
    metadata->insert(std::pair<grpc::string_ref, grpc::string_ref>(
        arr->metadata[i].key, grpc::string_ref(arr->metadata[i].value,
                                               arr->metadata[i].value_length)));
  }
  g_core_codegen_interface->grpc_metadata_array_destroy(arr);
  g_core_codegen_interface->grpc_metadata_array_init(arr);
}

// TODO(yangg) if the map is changed before we send, the pointers will be a
// mess. Make sure it does not happen.
inline grpc_metadata* FillMetadataArray(
    const std::multimap<grpc::string, grpc::string>& metadata) {
  if (metadata.empty()) {
    return nullptr;
  }
  grpc_metadata* metadata_array =
      (grpc_metadata*)(g_core_codegen_interface->gpr_malloc(
          metadata.size() * sizeof(grpc_metadata)));
  size_t i = 0;
  for (auto iter = metadata.cbegin(); iter != metadata.cend(); ++iter, ++i) {
    metadata_array[i].key = iter->first.c_str();
    metadata_array[i].value = iter->second.c_str();
    metadata_array[i].value_length = iter->second.size();
  }
  return metadata_array;
}

/// Per-message write options.
class WriteOptions {
 public:
  WriteOptions() : flags_(0) {}
  WriteOptions(const WriteOptions& other) : flags_(other.flags_) {}

  /// Clear all flags.
  inline void Clear() { flags_ = 0; }

  /// Returns raw flags bitset.
  inline uint32_t flags() const { return flags_; }

  /// Sets flag for the disabling of compression for the next message write.
  ///
  /// \sa GRPC_WRITE_NO_COMPRESS
  inline WriteOptions& set_no_compression() {
    SetBit(GRPC_WRITE_NO_COMPRESS);
    return *this;
  }

  /// Clears flag for the disabling of compression for the next message write.
  ///
  /// \sa GRPC_WRITE_NO_COMPRESS
  inline WriteOptions& clear_no_compression() {
    ClearBit(GRPC_WRITE_NO_COMPRESS);
    return *this;
  }

  /// Get value for the flag indicating whether compression for the next
  /// message write is forcefully disabled.
  ///
  /// \sa GRPC_WRITE_NO_COMPRESS
  inline bool get_no_compression() const {
    return GetBit(GRPC_WRITE_NO_COMPRESS);
  }

  /// Sets flag indicating that the write may be buffered and need not go out on
  /// the wire immediately.
  ///
  /// \sa GRPC_WRITE_BUFFER_HINT
  inline WriteOptions& set_buffer_hint() {
    SetBit(GRPC_WRITE_BUFFER_HINT);
    return *this;
  }

  /// Clears flag indicating that the write may be buffered and need not go out
  /// on the wire immediately.
  ///
  /// \sa GRPC_WRITE_BUFFER_HINT
  inline WriteOptions& clear_buffer_hint() {
    ClearBit(GRPC_WRITE_BUFFER_HINT);
    return *this;
  }

  /// Get value for the flag indicating that the write may be buffered and need
  /// not go out on the wire immediately.
  ///
  /// \sa GRPC_WRITE_BUFFER_HINT
  inline bool get_buffer_hint() const { return GetBit(GRPC_WRITE_BUFFER_HINT); }

  WriteOptions& operator=(const WriteOptions& rhs) {
    flags_ = rhs.flags_;
    return *this;
  }

 private:
  void SetBit(const uint32_t mask) { flags_ |= mask; }

  void ClearBit(const uint32_t mask) { flags_ &= ~mask; }

  bool GetBit(const uint32_t mask) const { return (flags_ & mask) != 0; }

  uint32_t flags_;
};

/// Default argument for CallOpSet. I is unused by the class, but can be
/// used for generating multiple names for the same thing.
template <int I>
class CallNoOp {
 protected:
  void AddOp(grpc_op* ops, size_t* nops) {}
  void FinishOp(bool* status, int max_receive_message_size) {}
};

class CallOpSendInitialMetadata {
 public:
  CallOpSendInitialMetadata() : send_(false) {
    maybe_compression_level_.is_set = false;
  }

  void SendInitialMetadata(
      const std::multimap<grpc::string, grpc::string>& metadata,
      uint32_t flags) {
    maybe_compression_level_.is_set = false;
    send_ = true;
    flags_ = flags;
    initial_metadata_count_ = metadata.size();
    initial_metadata_ = FillMetadataArray(metadata);
  }

  void set_compression_level(grpc_compression_level level) {
    maybe_compression_level_.is_set = true;
    maybe_compression_level_.level = level;
  }

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (!send_) return;
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->flags = flags_;
    op->reserved = NULL;
    op->data.send_initial_metadata.count = initial_metadata_count_;
    op->data.send_initial_metadata.metadata = initial_metadata_;
    op->data.send_initial_metadata.maybe_compression_level.is_set =
        maybe_compression_level_.is_set;
    op->data.send_initial_metadata.maybe_compression_level.level =
        maybe_compression_level_.level;
  }
  void FinishOp(bool* status, int max_receive_message_size) {
    if (!send_) return;
    g_core_codegen_interface->gpr_free(initial_metadata_);
    send_ = false;
  }

  bool send_;
  uint32_t flags_;
  size_t initial_metadata_count_;
  grpc_metadata* initial_metadata_;
  struct {
    bool is_set;
    grpc_compression_level level;
  } maybe_compression_level_;
};

class CallOpSendMessage {
 public:
  CallOpSendMessage() : send_buf_(nullptr), own_buf_(false) {}

  /// Send \a message using \a options for the write. The \a options are cleared
  /// after use.
  template <class M>
  Status SendMessage(const M& message,
                     const WriteOptions& options) GRPC_MUST_USE_RESULT;

  template <class M>
  Status SendMessage(const M& message) GRPC_MUST_USE_RESULT;

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (send_buf_ == nullptr) return;
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_SEND_MESSAGE;
    op->flags = write_options_.flags();
    op->reserved = NULL;
    op->data.send_message.send_message = send_buf_;
    // Flags are per-message: clear them after use.
    write_options_.Clear();
  }
  void FinishOp(bool* status, int max_receive_message_size) {
    if (own_buf_) g_core_codegen_interface->grpc_byte_buffer_destroy(send_buf_);
    send_buf_ = nullptr;
  }

 private:
  grpc_byte_buffer* send_buf_;
  WriteOptions write_options_;
  bool own_buf_;
};

template <class M>
Status CallOpSendMessage::SendMessage(const M& message,
                                      const WriteOptions& options) {
  write_options_ = options;
  return SerializationTraits<M>::Serialize(message, &send_buf_, &own_buf_);
}

template <class M>
Status CallOpSendMessage::SendMessage(const M& message) {
  return SendMessage(message, WriteOptions());
}

template <class R>
class CallOpRecvMessage {
 public:
  CallOpRecvMessage()
      : got_message(false),
        message_(nullptr),
        allow_not_getting_message_(false) {}

  void RecvMessage(R* message) { message_ = message; }

  // Do not change status if no message is received.
  void AllowNoMessage() { allow_not_getting_message_ = true; }

  bool got_message;

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (message_ == nullptr) return;
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_RECV_MESSAGE;
    op->flags = 0;
    op->reserved = NULL;
    op->data.recv_message.recv_message = &recv_buf_;
  }

  void FinishOp(bool* status, int max_receive_message_size) {
    if (message_ == nullptr) return;
    if (recv_buf_) {
      if (*status) {
        got_message = *status =
            SerializationTraits<R>::Deserialize(recv_buf_, message_,
                                                max_receive_message_size)
                .ok();
      } else {
        got_message = false;
        g_core_codegen_interface->grpc_byte_buffer_destroy(recv_buf_);
      }
    } else {
      got_message = false;
      if (!allow_not_getting_message_) {
        *status = false;
      }
    }
    message_ = nullptr;
  }

 private:
  R* message_;
  grpc_byte_buffer* recv_buf_;
  bool allow_not_getting_message_;
};

namespace CallOpGenericRecvMessageHelper {
class DeserializeFunc {
 public:
  virtual Status Deserialize(grpc_byte_buffer* buf,
                             int max_receive_message_size) = 0;
  virtual ~DeserializeFunc() {}
};

template <class R>
class DeserializeFuncType final : public DeserializeFunc {
 public:
  DeserializeFuncType(R* message) : message_(message) {}
  Status Deserialize(grpc_byte_buffer* buf,
                     int max_receive_message_size) override {
    return SerializationTraits<R>::Deserialize(buf, message_,
                                               max_receive_message_size);
  }

  ~DeserializeFuncType() override {}

 private:
  R* message_;  // Not a managed pointer because management is external to this
};
}  // namespace CallOpGenericRecvMessageHelper

class CallOpGenericRecvMessage {
 public:
  CallOpGenericRecvMessage()
      : got_message(false), allow_not_getting_message_(false) {}

  template <class R>
  void RecvMessage(R* message) {
    // Use an explicit base class pointer to avoid resolution error in the
    // following unique_ptr::reset for some old implementations.
    CallOpGenericRecvMessageHelper::DeserializeFunc* func =
        new CallOpGenericRecvMessageHelper::DeserializeFuncType<R>(message);
    deserialize_.reset(func);
  }

  // Do not change status if no message is received.
  void AllowNoMessage() { allow_not_getting_message_ = true; }

  bool got_message;

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (!deserialize_) return;
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_RECV_MESSAGE;
    op->flags = 0;
    op->reserved = NULL;
    op->data.recv_message.recv_message = &recv_buf_;
  }

  void FinishOp(bool* status, int max_receive_message_size) {
    if (!deserialize_) return;
    if (recv_buf_) {
      if (*status) {
        got_message = true;
        *status =
            deserialize_->Deserialize(recv_buf_, max_receive_message_size).ok();
      } else {
        got_message = false;
        g_core_codegen_interface->grpc_byte_buffer_destroy(recv_buf_);
      }
    } else {
      got_message = false;
      if (!allow_not_getting_message_) {
        *status = false;
      }
    }
    deserialize_.reset();
  }

 private:
  std::unique_ptr<CallOpGenericRecvMessageHelper::DeserializeFunc> deserialize_;
  grpc_byte_buffer* recv_buf_;
  bool allow_not_getting_message_;
};

class CallOpClientSendClose {
 public:
  CallOpClientSendClose() : send_(false) {}

  void ClientSendClose() { send_ = true; }

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (!send_) return;
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op->flags = 0;
    op->reserved = NULL;
  }
  void FinishOp(bool* status, int max_receive_message_size) { send_ = false; }

 private:
  bool send_;
};

class CallOpServerSendStatus {
 public:
  CallOpServerSendStatus() : send_status_available_(false) {}

  void ServerSendStatus(
      const std::multimap<grpc::string, grpc::string>& trailing_metadata,
      const Status& status) {
    trailing_metadata_count_ = trailing_metadata.size();
    trailing_metadata_ = FillMetadataArray(trailing_metadata);
    send_status_available_ = true;
    send_status_code_ = static_cast<grpc_status_code>(GetCanonicalCode(status));
    send_status_details_ = status.error_message();
  }

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (!send_status_available_) return;
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op->data.send_status_from_server.trailing_metadata_count =
        trailing_metadata_count_;
    op->data.send_status_from_server.trailing_metadata = trailing_metadata_;
    op->data.send_status_from_server.status = send_status_code_;
    op->data.send_status_from_server.status_details =
        send_status_details_.empty() ? nullptr : send_status_details_.c_str();
    op->flags = 0;
    op->reserved = NULL;
  }

  void FinishOp(bool* status, int max_receive_message_size) {
    if (!send_status_available_) return;
    g_core_codegen_interface->gpr_free(trailing_metadata_);
    send_status_available_ = false;
  }

 private:
  bool send_status_available_;
  grpc_status_code send_status_code_;
  grpc::string send_status_details_;
  size_t trailing_metadata_count_;
  grpc_metadata* trailing_metadata_;
};

class CallOpRecvInitialMetadata {
 public:
  CallOpRecvInitialMetadata() : recv_initial_metadata_(nullptr) {}

  void RecvInitialMetadata(ClientContext* context) {
    context->initial_metadata_received_ = true;
    recv_initial_metadata_ = &context->recv_initial_metadata_;
  }

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (!recv_initial_metadata_) return;
    memset(&recv_initial_metadata_arr_, 0, sizeof(recv_initial_metadata_arr_));
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &recv_initial_metadata_arr_;
    op->flags = 0;
    op->reserved = NULL;
  }
  void FinishOp(bool* status, int max_receive_message_size) {
    if (recv_initial_metadata_ == nullptr) return;
    FillMetadataMap(&recv_initial_metadata_arr_, recv_initial_metadata_);
    recv_initial_metadata_ = nullptr;
  }

 private:
  std::multimap<grpc::string_ref, grpc::string_ref>* recv_initial_metadata_;
  grpc_metadata_array recv_initial_metadata_arr_;
};

class CallOpClientRecvStatus {
 public:
  CallOpClientRecvStatus() : recv_status_(nullptr) {}

  void ClientRecvStatus(ClientContext* context, Status* status) {
    recv_trailing_metadata_ = &context->trailing_metadata_;
    recv_status_ = status;
  }

 protected:
  void AddOp(grpc_op* ops, size_t* nops) {
    if (recv_status_ == nullptr) return;
    memset(&recv_trailing_metadata_arr_, 0,
           sizeof(recv_trailing_metadata_arr_));
    status_details_ = nullptr;
    status_details_capacity_ = 0;
    grpc_op* op = &ops[(*nops)++];
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata =
        &recv_trailing_metadata_arr_;
    op->data.recv_status_on_client.status = &status_code_;
    op->data.recv_status_on_client.status_details = &status_details_;
    op->data.recv_status_on_client.status_details_capacity =
        &status_details_capacity_;
    op->flags = 0;
    op->reserved = NULL;
  }

  void FinishOp(bool* status, int max_receive_message_size) {
    if (recv_status_ == nullptr) return;
    FillMetadataMap(&recv_trailing_metadata_arr_, recv_trailing_metadata_);
    *recv_status_ = Status(
        static_cast<StatusCode>(status_code_),
        status_details_ ? grpc::string(status_details_) : grpc::string());
    g_core_codegen_interface->gpr_free(status_details_);
    recv_status_ = nullptr;
  }

 private:
  std::multimap<grpc::string_ref, grpc::string_ref>* recv_trailing_metadata_;
  Status* recv_status_;
  grpc_metadata_array recv_trailing_metadata_arr_;
  grpc_status_code status_code_;
  char* status_details_;
  size_t status_details_capacity_;
};

/// An abstract collection of CallOpSet's, to be used whenever
/// CallOpSet objects must be thought of as a group. Each member
/// of the group should have a shared_ptr back to the collection,
/// as will the object that instantiates the collection, allowing
/// for automatic ref-counting. In practice, any actual use should
/// derive from this base class. This is specifically necessary if
/// some of the CallOpSet's in the collection are "Sneaky" and don't
/// report back to the C++ layer CQ operations
class CallOpSetCollectionInterface
    : public std::enable_shared_from_this<CallOpSetCollectionInterface> {};

/// An abstract collection of call ops, used to generate the
/// grpc_call_op structure to pass down to the lower layers,
/// and as it is-a CompletionQueueTag, also massages the final
/// completion into the correct form for consumption in the C++
/// API.
class CallOpSetInterface : public CompletionQueueTag {
 public:
  CallOpSetInterface() : max_receive_message_size_(0) {}
  /// Fills in grpc_op, starting from ops[*nops] and moving
  /// upwards.
  virtual void FillOps(grpc_op* ops, size_t* nops) = 0;

  void set_max_receive_message_size(int max_receive_message_size) {
    max_receive_message_size_ = max_receive_message_size;
  }

  /// Mark this as belonging to a collection if needed
  void SetCollection(std::shared_ptr<CallOpSetCollectionInterface> collection) {
    collection_ = collection;
  }

 protected:
  int max_receive_message_size_;
  std::shared_ptr<CallOpSetCollectionInterface> collection_;
};

/// Primary implementaiton of CallOpSetInterface.
/// Since we cannot use variadic templates, we declare slots up to
/// the maximum count of ops we'll need in a set. We leverage the
/// empty base class optimization to slim this class (especially
/// when there are many unused slots used). To avoid duplicate base classes,
/// the template parmeter for CallNoOp is varied by argument position.
template <class Op1 = CallNoOp<1>, class Op2 = CallNoOp<2>,
          class Op3 = CallNoOp<3>, class Op4 = CallNoOp<4>,
          class Op5 = CallNoOp<5>, class Op6 = CallNoOp<6>>
class CallOpSet : public CallOpSetInterface,
                  public Op1,
                  public Op2,
                  public Op3,
                  public Op4,
                  public Op5,
                  public Op6 {
 public:
  CallOpSet() : return_tag_(this) {}
  void FillOps(grpc_op* ops, size_t* nops) override {
    this->Op1::AddOp(ops, nops);
    this->Op2::AddOp(ops, nops);
    this->Op3::AddOp(ops, nops);
    this->Op4::AddOp(ops, nops);
    this->Op5::AddOp(ops, nops);
    this->Op6::AddOp(ops, nops);
  }

  bool FinalizeResult(void** tag, bool* status) override {
    this->Op1::FinishOp(status, max_receive_message_size_);
    this->Op2::FinishOp(status, max_receive_message_size_);
    this->Op3::FinishOp(status, max_receive_message_size_);
    this->Op4::FinishOp(status, max_receive_message_size_);
    this->Op5::FinishOp(status, max_receive_message_size_);
    this->Op6::FinishOp(status, max_receive_message_size_);
    *tag = return_tag_;
    collection_.reset();  // drop the ref at this point
    return true;
  }

  void set_output_tag(void* return_tag) { return_tag_ = return_tag; }

 private:
  void* return_tag_;
};

/// A CallOpSet that does not post completions to the completion queue.
///
/// Allows hiding some completions that the C core must generate from
/// C++ users.
template <class Op1 = CallNoOp<1>, class Op2 = CallNoOp<2>,
          class Op3 = CallNoOp<3>, class Op4 = CallNoOp<4>,
          class Op5 = CallNoOp<5>, class Op6 = CallNoOp<6>>
class SneakyCallOpSet : public CallOpSet<Op1, Op2, Op3, Op4, Op5, Op6> {
 public:
  bool FinalizeResult(void** tag, bool* status) override {
    typedef CallOpSet<Op1, Op2, Op3, Op4, Op5, Op6> Base;
    return Base::FinalizeResult(tag, status) && false;
  }
};

// Straightforward wrapping of the C call object
class Call final {
 public:
  /* call is owned by the caller */
  Call(grpc_call* call, CallHook* call_hook, CompletionQueue* cq)
      : call_hook_(call_hook),
        cq_(cq),
        call_(call),
        max_receive_message_size_(-1) {}

  Call(grpc_call* call, CallHook* call_hook, CompletionQueue* cq,
       int max_receive_message_size)
      : call_hook_(call_hook),
        cq_(cq),
        call_(call),
        max_receive_message_size_(max_receive_message_size) {}

  void PerformOps(CallOpSetInterface* ops) {
    if (max_receive_message_size_ > 0) {
      ops->set_max_receive_message_size(max_receive_message_size_);
    }
    call_hook_->PerformOpsOnCall(ops, this);
  }

  grpc_call* call() const { return call_; }
  CompletionQueue* cq() const { return cq_; }

  int max_receive_message_size() { return max_receive_message_size_; }

 private:
  CallHook* call_hook_;
  CompletionQueue* cq_;
  grpc_call* call_;
  int max_receive_message_size_;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_CALL_H
