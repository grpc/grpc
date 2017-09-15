/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCXX_IMPL_CODEGEN_BYTE_BUFFER_H
#define GRPCXX_IMPL_CODEGEN_BYTE_BUFFER_H

#include <grpc/impl/codegen/byte_buffer.h>

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/serialization_traits.h>
#include <grpc++/impl/codegen/slice.h>
#include <grpc++/impl/codegen/status.h>

#include <vector>

namespace grpc {

template <class R>
class CallOpRecvMessage;
class MethodHandler;
template <class ServiceType, class RequestType, class ResponseType>
class RpcMethodHandler;
template <class ServiceType, class RequestType, class ResponseType>
class ServerStreamingHandler;
namespace CallOpGenericRecvMessageHelper {
template <class R>
class DeserializeFuncType;
}  // namespace CallOpGenericRecvMessageHelper

/// A sequence of bytes.
class ByteBuffer final {
 public:
  /// Constuct an empty buffer.
  ByteBuffer() : buffer_(nullptr) {}

  /// Construct buffer from \a slices, of which there are \a nslices.
  ByteBuffer(const Slice* slices, size_t nslices);

  /// Constuct a byte buffer by referencing elements of existing buffer
  /// \a buf. Wrapper of core function grpc_byte_buffer_copy
  ByteBuffer(const ByteBuffer& buf);

  ~ByteBuffer() {
    if (buffer_) {
      g_core_codegen_interface->grpc_byte_buffer_destroy(buffer_);
    }
  }

  ByteBuffer& operator=(const ByteBuffer&);

  /// Dump (read) the buffer contents into \a slices.
  Status Dump(std::vector<Slice>* slices) const;

  /// Remove all data.
  void Clear() {
    if (buffer_) {
      g_core_codegen_interface->grpc_byte_buffer_destroy(buffer_);
      buffer_ = nullptr;
    }
  }

  /// Make a duplicate copy of the internals of this byte
  /// buffer so that we have our own owned version of it.
  /// bbuf.Duplicate(); is equivalent to bbuf=bbuf; but is actually readable
  void Duplicate() {
    buffer_ = g_core_codegen_interface->grpc_byte_buffer_copy(buffer_);
  }

  /// Forget underlying byte buffer without destroying
  /// Use this only for un-owned byte buffers
  void Release() { buffer_ = nullptr; }

  /// Buffer size in bytes.
  size_t Length() const;

  /// Swap the state of *this and *other.
  void Swap(ByteBuffer* other);

  /// Is this ByteBuffer valid?
  bool Valid() const { return (buffer_ != nullptr); }

 private:
  friend class SerializationTraits<ByteBuffer, void>;
  friend class CallOpSendMessage;
  template <class R>
  friend class CallOpRecvMessage;
  friend class CallOpGenericRecvMessage;
  friend class MethodHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class RpcMethodHandler;
  template <class ServiceType, class RequestType, class ResponseType>
  friend class ServerStreamingHandler;
  template <class R>
  friend class CallOpGenericRecvMessageHelper::DeserializeFuncType;

  grpc_byte_buffer* buffer_;

  // takes ownership
  void set_buffer(grpc_byte_buffer* buf) {
    if (buffer_) {
      Clear();
    }
    buffer_ = buf;
  }

  grpc_byte_buffer* c_buffer() { return buffer_; }
  grpc_byte_buffer** c_buffer_ptr() { return &buffer_; }

  class ByteBufferPointer {
   public:
    ByteBufferPointer(const ByteBuffer* b)
        : bbuf_(const_cast<ByteBuffer*>(b)) {}
    operator ByteBuffer*() { return bbuf_; }
    operator grpc_byte_buffer*() { return bbuf_->buffer_; }
    operator grpc_byte_buffer**() { return &bbuf_->buffer_; }

   private:
    ByteBuffer* bbuf_;
  };
  ByteBufferPointer bbuf_ptr() const { return ByteBufferPointer(this); }
};

template <>
class SerializationTraits<ByteBuffer, void> {
 public:
  static Status Deserialize(ByteBuffer* byte_buffer, ByteBuffer* dest) {
    dest->set_buffer(byte_buffer->buffer_);
    return Status::OK;
  }
  static Status Serialize(const ByteBuffer& source, ByteBuffer* buffer,
                          bool* own_buffer) {
    *buffer = source;
    *own_buffer = true;
    return Status::OK;
  }
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_BYTE_BUFFER_H
