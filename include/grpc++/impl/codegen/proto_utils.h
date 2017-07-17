/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPCXX_IMPL_CODEGEN_PROTO_UTILS_H
#define GRPCXX_IMPL_CODEGEN_PROTO_UTILS_H

#include <type_traits>

#include <grpc++/impl/codegen/config_protobuf.h>
#include <grpc++/impl/codegen/core_codegen_interface.h>
#include <grpc++/impl/codegen/serialization_traits.h>
#include <grpc++/impl/codegen/status.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/impl/codegen/slice.h>

namespace grpc {

extern CoreCodegenInterface* g_core_codegen_interface;

namespace internal {

class GrpcBufferWriterPeer;

const int kGrpcBufferWriterMaxBufferLength = 1024 * 1024;

class GrpcBufferWriter final
    : public ::grpc::protobuf::io::ZeroCopyOutputStream {
 public:
  explicit GrpcBufferWriter(grpc_byte_buffer** bp, int block_size)
      : block_size_(block_size), byte_count_(0), have_backup_(false) {
    *bp = g_core_codegen_interface->grpc_raw_byte_buffer_create(NULL, 0);
    slice_buffer_ = &(*bp)->data.raw.slice_buffer;
  }

  ~GrpcBufferWriter() override {
    if (have_backup_) {
      g_core_codegen_interface->grpc_slice_unref(backup_slice_);
    }
  }

  bool Next(void** data, int* size) override {
    if (have_backup_) {
      slice_ = backup_slice_;
      have_backup_ = false;
    } else {
      slice_ = g_core_codegen_interface->grpc_slice_malloc(block_size_);
    }
    *data = GRPC_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_CODEGEN_ASSERT(GRPC_SLICE_LENGTH(slice_) <= INT_MAX);
    byte_count_ += * size = (int)GRPC_SLICE_LENGTH(slice_);
    g_core_codegen_interface->grpc_slice_buffer_add(slice_buffer_, slice_);
    return true;
  }

  void BackUp(int count) override {
    g_core_codegen_interface->grpc_slice_buffer_pop(slice_buffer_);
    if (count == block_size_) {
      backup_slice_ = slice_;
    } else {
      backup_slice_ = g_core_codegen_interface->grpc_slice_split_tail(
          &slice_, GRPC_SLICE_LENGTH(slice_) - count);
      g_core_codegen_interface->grpc_slice_buffer_add(slice_buffer_, slice_);
    }
    // It's dangerous to keep an inlined grpc_slice as the backup slice, since
    // on a following Next() call, a reference will be returned to this slice
    // via GRPC_SLICE_START_PTR, which will not be an adddress held by
    // slice_buffer_.
    have_backup_ = backup_slice_.refcount != NULL;
    byte_count_ -= count;
  }

  grpc::protobuf::int64 ByteCount() const override { return byte_count_; }

 private:
  friend class GrpcBufferWriterPeer;
  const int block_size_;
  int64_t byte_count_;
  grpc_slice_buffer* slice_buffer_;
  bool have_backup_;
  grpc_slice backup_slice_;
  grpc_slice slice_;
};

class GrpcBufferReader final
    : public ::grpc::protobuf::io::ZeroCopyInputStream {
 public:
  explicit GrpcBufferReader(grpc_byte_buffer* buffer)
      : byte_count_(0), backup_count_(0), status_() {
    if (!g_core_codegen_interface->grpc_byte_buffer_reader_init(&reader_,
                                                                buffer)) {
      status_ = Status(StatusCode::INTERNAL,
                       "Couldn't initialize byte buffer reader");
    }
  }
  ~GrpcBufferReader() override {
    g_core_codegen_interface->grpc_byte_buffer_reader_destroy(&reader_);
  }

  bool Next(const void** data, int* size) override {
    if (!status_.ok()) {
      return false;
    }
    if (backup_count_ > 0) {
      *data = GRPC_SLICE_START_PTR(slice_) + GRPC_SLICE_LENGTH(slice_) -
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
    g_core_codegen_interface->grpc_slice_unref(slice_);
    *data = GRPC_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_CODEGEN_ASSERT(GRPC_SLICE_LENGTH(slice_) <= INT_MAX);
    byte_count_ += * size = (int)GRPC_SLICE_LENGTH(slice_);
    return true;
  }

  Status status() const { return status_; }

  void BackUp(int count) override { backup_count_ = count; }

  bool Skip(int count) override {
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

  grpc::protobuf::int64 ByteCount() const override {
    return byte_count_ - backup_count_;
  }

 private:
  int64_t byte_count_;
  int64_t backup_count_;
  grpc_byte_buffer_reader reader_;
  grpc_slice slice_;
  Status status_;
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
      grpc_slice slice = g_core_codegen_interface->grpc_slice_malloc(byte_size);
      GPR_CODEGEN_ASSERT(
          GRPC_SLICE_END_PTR(slice) ==
          msg.SerializeWithCachedSizesToArray(GRPC_SLICE_START_PTR(slice)));
      *bp = g_core_codegen_interface->grpc_raw_byte_buffer_create(&slice, 1);
      g_core_codegen_interface->grpc_slice_unref(slice);
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
                            grpc::protobuf::Message* msg) {
    if (buffer == nullptr) {
      return Status(StatusCode::INTERNAL, "No payload");
    }
    Status result = g_core_codegen_interface->ok();
    {
      internal::GrpcBufferReader reader(buffer);
      if (!reader.status().ok()) {
        return reader.status();
      }
      ::grpc::protobuf::io::CodedInputStream decoder(&reader);
      decoder.SetTotalBytesLimit(INT_MAX, INT_MAX);
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
