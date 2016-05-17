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

#ifndef GRPCXX_IMPL_CODEGEN_PROTO_UTILS_H
#define GRPCXX_IMPL_CODEGEN_PROTO_UTILS_H

#include <type_traits>

#include <grpc++/impl/codegen/config_protobuf.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/serialization_traits.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc/impl/codegen/byte_buffer.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/log.h>
#include <grpc/impl/codegen/slice.h>

namespace grpc {

extern CoreCodegenInterface* g_core_codegen_interface;

namespace internal {

const int kGrpcBufferWriterMaxBufferLength = 8192;

class GrpcBufferWriter GRPC_FINAL
    : public ::grpc::protobuf::io::ZeroCopyOutputStream {
 public:
  explicit GrpcBufferWriter(grpc_byte_buffer** bp, int block_size)
      : block_size_(block_size), byte_count_(0), have_backup_(false) {
    *bp = g_core_codegen_interface->grpc_raw_byte_buffer_create(NULL, 0);
    slice_buffer_ = &(*bp)->data.raw.slice_buffer;
  }

  ~GrpcBufferWriter() GRPC_OVERRIDE {
    if (have_backup_) {
      g_core_codegen_interface->gpr_slice_unref(backup_slice_);
    }
  }

  bool Next(void** data, int* size) GRPC_OVERRIDE {
    if (have_backup_) {
      slice_ = backup_slice_;
      have_backup_ = false;
    } else {
      slice_ = g_core_codegen_interface->gpr_slice_malloc(block_size_);
    }
    *data = GPR_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_CODEGEN_ASSERT(GPR_SLICE_LENGTH(slice_) <= INT_MAX);
    byte_count_ += * size = (int)GPR_SLICE_LENGTH(slice_);
    g_core_codegen_interface->gpr_slice_buffer_add(slice_buffer_, slice_);
    return true;
  }

  void BackUp(int count) GRPC_OVERRIDE {
    g_core_codegen_interface->gpr_slice_buffer_pop(slice_buffer_);
    if (count == block_size_) {
      backup_slice_ = slice_;
    } else {
      backup_slice_ = g_core_codegen_interface->gpr_slice_split_tail(
          &slice_, GPR_SLICE_LENGTH(slice_) - count);
      g_core_codegen_interface->gpr_slice_buffer_add(slice_buffer_, slice_);
    }
    have_backup_ = true;
    byte_count_ -= count;
  }

  grpc::protobuf::int64 ByteCount() const GRPC_OVERRIDE { return byte_count_; }

 private:
  const int block_size_;
  int64_t byte_count_;
  gpr_slice_buffer* slice_buffer_;
  bool have_backup_;
  gpr_slice backup_slice_;
  gpr_slice slice_;
};

class GrpcBufferReader GRPC_FINAL
    : public ::grpc::protobuf::io::ZeroCopyInputStream {
 public:
  explicit GrpcBufferReader(grpc_byte_buffer* buffer)
      : byte_count_(0), backup_count_(0) {
    g_core_codegen_interface->grpc_byte_buffer_reader_init(&reader_, buffer);
  }
  ~GrpcBufferReader() GRPC_OVERRIDE {
    g_core_codegen_interface->grpc_byte_buffer_reader_destroy(&reader_);
  }

  bool Next(const void** data, int* size) GRPC_OVERRIDE {
    if (backup_count_ > 0) {
      *data = GPR_SLICE_START_PTR(slice_) + GPR_SLICE_LENGTH(slice_) -
              backup_count_;
      GPR_CODEGEN_ASSERT(backup_count_ <= INT_MAX);
      *size = (int)backup_count_;
      backup_count_ = 0;
      return true;
    }
    if (!g_core_codegen_interface->grpc_byte_buffer_reader_next(&reader_,
                                                                &slice_)) {
      return false;
    }
    g_core_codegen_interface->gpr_slice_unref(slice_);
    *data = GPR_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_CODEGEN_ASSERT(GPR_SLICE_LENGTH(slice_) <= INT_MAX);
    byte_count_ += * size = (int)GPR_SLICE_LENGTH(slice_);
    return true;
  }

  void BackUp(int count) GRPC_OVERRIDE { backup_count_ = count; }

  bool Skip(int count) GRPC_OVERRIDE {
    const void* data;
    int size;
    while (Next(&data, &size)) {
      if (size >= count) {
        BackUp(size - count);
        return true;
      }
      // size < count;
      count -= size;
    }
    // error or we have too large count;
    return false;
  }

  grpc::protobuf::int64 ByteCount() const GRPC_OVERRIDE {
    return byte_count_ - backup_count_;
  }

 private:
  int64_t byte_count_;
  int64_t backup_count_;
  grpc_byte_buffer_reader reader_;
  gpr_slice slice_;
};
}  // namespace internal

template <class T>
class SerializationTraits<T, typename std::enable_if<std::is_base_of<
                                 grpc::protobuf::Message, T>::value>::type> {
 public:
  static Status Serialize(const grpc::protobuf::Message& msg,
                          grpc_byte_buffer** bp, bool* own_buffer) {
    *own_buffer = true;
    int byte_size = msg.ByteSize();
    if (byte_size <= internal::kGrpcBufferWriterMaxBufferLength) {
      gpr_slice slice = g_core_codegen_interface->gpr_slice_malloc(byte_size);
      GPR_CODEGEN_ASSERT(
          GPR_SLICE_END_PTR(slice) ==
          msg.SerializeWithCachedSizesToArray(GPR_SLICE_START_PTR(slice)));
      *bp = g_core_codegen_interface->grpc_raw_byte_buffer_create(&slice, 1);
      g_core_codegen_interface->gpr_slice_unref(slice);
      return g_core_codegen_interface->ok();
    } else {
      internal::GrpcBufferWriter writer(
          bp, internal::kGrpcBufferWriterMaxBufferLength);
      return msg.SerializeToZeroCopyStream(&writer)
                 ? g_core_codegen_interface->ok()
                 : Status(StatusCode::INTERNAL, "Failed to serialize message");
    }
  }

  static Status Deserialize(grpc_byte_buffer* buffer,
                            grpc::protobuf::Message* msg,
                            int max_message_size) {
    if (buffer == nullptr) {
      return Status(StatusCode::INTERNAL, "No payload");
    }
    Status result = g_core_codegen_interface->ok();
    {
      internal::GrpcBufferReader reader(buffer);
      ::grpc::protobuf::io::CodedInputStream decoder(&reader);
      if (max_message_size > 0) {
        decoder.SetTotalBytesLimit(max_message_size, max_message_size);
      }
      if (!msg->ParseFromCodedStream(&decoder)) {
        result = Status(StatusCode::INTERNAL, msg->InitializationErrorString());
      }
      if (!decoder.ConsumedEntireMessage()) {
        result = Status(StatusCode::INTERNAL, "Did not read entire message");
      }
    }
    g_core_codegen_interface->grpc_byte_buffer_destroy(buffer);
    return result;
  }
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_PROTO_UTILS_H
