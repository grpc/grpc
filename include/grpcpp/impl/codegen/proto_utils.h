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

#ifndef GRPCPP_IMPL_CODEGEN_PROTO_UTILS_H
#define GRPCPP_IMPL_CODEGEN_PROTO_UTILS_H

#include <type_traits>

#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/impl/codegen/slice.h>
#include <grpcpp/impl/codegen/config_protobuf.h>
#include <grpcpp/impl/codegen/core_codegen_interface.h>
#include <grpcpp/impl/codegen/serialization_traits.h>
#include <grpcpp/impl/codegen/status.h>

/// This header provides serialization and deserialization between gRPC
/// messages serialized using protobuf and the C++ objects they represent.

namespace grpc {
namespace internal {

// Forward declaration for testing use only
class GrpcBufferWriterPeer;

const int kGrpcBufferWriterMaxBufferLength = 1024 * 1024;

/// This is a specialization of the protobuf class ZeroCopyOutputStream.
/// The principle is to give the proto layer one buffer of bytes at a time
/// that it can use to serialize the next portion of the message, with the
/// option to "backup" if more buffer is given than required at the last buffer.
class GrpcBufferWriter : public ::grpc::protobuf::io::ZeroCopyOutputStream {
 public:
  /// Constructor for this derived class
  ///
  /// \param[out] bp A pointer to the byte buffer created
  /// \param block_size How big are the chunks to allocate at a time
  /// \param total_size How many total bytes are required for this proto
  GrpcBufferWriter(grpc_byte_buffer** bp, int block_size, int total_size)
      : block_size_(block_size),
        total_size_(total_size),
        byte_count_(0),
        have_backup_(false) {
    /// Create an empty raw byte buffer and look at its underlying slice buffer
    *bp = g_core_codegen_interface->grpc_raw_byte_buffer_create(NULL, 0);
    slice_buffer_ = &(*bp)->data.raw.slice_buffer;
  }

  ~GrpcBufferWriter() override {
    if (have_backup_) {
      g_core_codegen_interface->grpc_slice_unref(backup_slice_);
    }
  }

  /// Give the proto library the next buffer of bytes and its size
  bool Next(void** data, int* size) override {
    // 1. Use the remaining backup slice if we have one
    // 2. Otherwise allocate a slice, up to the remaining length needed
    //    or our maximum allocation size
    // 3. Provide the slice start and size available
    // 4. Add the slice being returned to the slice buffer
    GPR_CODEGEN_ASSERT(byte_count_ < total_size_);
    size_t remain = total_size_ - byte_count_;
    if (have_backup_) {
      /// If we have a backup slice, we should use it first
      slice_ = backup_slice_;
      have_backup_ = false;
      if (GRPC_SLICE_LENGTH(slice_) > remain) {
        GRPC_SLICE_SET_LENGTH(slice_, remain);
      }
    } else {
      // When less than a whole block is needed, only allocate that much.
      // But make sure the allocated slice is not inlined
      size_t allocate_length =
          remain > static_cast<size_t>(block_size_) ? block_size_ : remain;
      slice_ = g_core_codegen_interface->grpc_slice_malloc(
          allocate_length > GRPC_SLICE_INLINED_SIZE
              ? allocate_length
              : GRPC_SLICE_INLINED_SIZE + 1);
    }
    *data = GRPC_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_CODEGEN_ASSERT(GRPC_SLICE_LENGTH(slice_) <= INT_MAX);
    *size = (int)GRPC_SLICE_LENGTH(slice_);
    byte_count_ += *size;
    g_core_codegen_interface->grpc_slice_buffer_add(slice_buffer_, slice_);
    return true;
  }

  /// Backup by \a count bytes because Next returned more bytes than needed
  /// (only used in the last buffer)
  void BackUp(int count) override {
    /// 1. Remove the partially-used last slice from the slice buffer
    /// 2. Split it into the needed (if any) and unneeded part
    /// 3. Add the needed part back to the slice buffer
    /// 4. Mark that we still have the remaining part (for later use/unref)
    g_core_codegen_interface->grpc_slice_buffer_pop(slice_buffer_);
    if ((size_t)count == GRPC_SLICE_LENGTH(slice_)) {
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

  /// Returns the number of bytes allocated for this message to be written
  grpc::protobuf::int64 ByteCount() const override { return byte_count_; }

 protected:
  // friend for testing purposes only
  friend class GrpcBufferWriterPeer;

  const int block_size_;
  const int total_size_;
  int64_t byte_count_;
  grpc_slice_buffer* slice_buffer_;
  bool have_backup_;
  grpc_slice backup_slice_;
  grpc_slice slice_;
};

/// This is a specialization of the protobuf class ZeroCopyInputStream
/// The principle is to get one chunk of data at a time from the proto layer,
/// with options to backup (re-see some bytes) or skip (forward past some bytes)
class GrpcBufferReader : public ::grpc::protobuf::io::ZeroCopyInputStream {
 public:
  explicit GrpcBufferReader(grpc_byte_buffer* buffer)
      : byte_count_(0), backup_count_(0), status_() {
    /// Implemented through a grpc_byte_buffer_reader which iterates
    /// over the slices that make up a byte buffer
    if (!g_core_codegen_interface->grpc_byte_buffer_reader_init(&reader_,
                                                                buffer)) {
      status_ = Status(StatusCode::INTERNAL,
                       "Couldn't initialize byte buffer reader");
    }
  }
  ~GrpcBufferReader() override {
    g_core_codegen_interface->grpc_byte_buffer_reader_destroy(&reader_);
  }

  /// Give the proto library a chunk of data from the stream
  bool Next(const void** data, int* size) override {
    if (!status_.ok()) {
      return false;
    }
    /// If we have backed up previously, we need to return the backed-up slice
    if (backup_count_ > 0) {
      *data = GRPC_SLICE_START_PTR(slice_) + GRPC_SLICE_LENGTH(slice_) -
              backup_count_;
      GPR_CODEGEN_ASSERT(backup_count_ <= INT_MAX);
      *size = (int)backup_count_;
      backup_count_ = 0;
      return true;
    }
    /// Otherwise get the next slice from the byte buffer reader
    if (!g_core_codegen_interface->grpc_byte_buffer_reader_next(&reader_,
                                                                &slice_)) {
      return false;
    }
    g_core_codegen_interface->grpc_slice_unref(slice_);
    *data = GRPC_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_CODEGEN_ASSERT(GRPC_SLICE_LENGTH(slice_) <= INT_MAX);
    *size = (int)GRPC_SLICE_LENGTH(slice_);
    byte_count_ += *size;
    return true;
  }

  Status status() const { return status_; }

  /// The proto library calls this to indicate that we should back up a number
  /// of bytes that have already been returned by the last call of Next.
  /// So do the backup and have that ready for a later Next.
  void BackUp(int count) override { backup_count_ = count; }

  /// The proto library calls this to skip over some bytes. Implement this using
  /// Next and BackUp combined.
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

  /// How many bytes have actually been fed to the proto library
  grpc::protobuf::int64 ByteCount() const override {
    return byte_count_ - backup_count_;
  }

 protected:
  int64_t byte_count_;
  int64_t backup_count_;
  grpc_byte_buffer_reader reader_;
  grpc_slice slice_;
  Status status_;
};

// BufferWriter must be a subclass of io::ZeroCopyOutputStream.
template <class BufferWriter, class T>
Status GenericSerialize(const grpc::protobuf::Message& msg,
                        grpc_byte_buffer** bp, bool* own_buffer) {
  static_assert(
      std::is_base_of<protobuf::io::ZeroCopyOutputStream, BufferWriter>::value,
      "BufferWriter must be a subclass of io::ZeroCopyOutputStream");
  *own_buffer = true;
  int byte_size = msg.ByteSize();
  if ((size_t)byte_size <= GRPC_SLICE_INLINED_SIZE) {
    grpc_slice slice = g_core_codegen_interface->grpc_slice_malloc(byte_size);
    GPR_CODEGEN_ASSERT(
        GRPC_SLICE_END_PTR(slice) ==
        msg.SerializeWithCachedSizesToArray(GRPC_SLICE_START_PTR(slice)));
    *bp = g_core_codegen_interface->grpc_raw_byte_buffer_create(&slice, 1);
    g_core_codegen_interface->grpc_slice_unref(slice);

    return g_core_codegen_interface->ok();
  }
  BufferWriter writer(bp, kGrpcBufferWriterMaxBufferLength, byte_size);
  return msg.SerializeToZeroCopyStream(&writer)
             ? g_core_codegen_interface->ok()
             : Status(StatusCode::INTERNAL, "Failed to serialize message");
}

// BufferReader must be a subclass of io::ZeroCopyInputStream.
template <class BufferReader, class T>
Status GenericDeserialize(grpc_byte_buffer* buffer,
                          grpc::protobuf::Message* msg) {
  static_assert(
      std::is_base_of<protobuf::io::ZeroCopyInputStream, BufferReader>::value,
      "BufferReader must be a subclass of io::ZeroCopyInputStream");
  if (buffer == nullptr) {
    return Status(StatusCode::INTERNAL, "No payload");
  }
  Status result = g_core_codegen_interface->ok();
  {
    BufferReader reader(buffer);
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

}  // namespace internal

// this is needed so the following class does not conflict with protobuf
// serializers that utilize internal-only tools.
#ifdef GRPC_OPEN_SOURCE_PROTO
// This class provides a protobuf serializer. It translates between protobuf
// objects and grpc_byte_buffers. More information about SerializationTraits can
// be found in include/grpcpp/impl/codegen/serialization_traits.h.
template <class T>
class SerializationTraits<T, typename std::enable_if<std::is_base_of<
                                 grpc::protobuf::Message, T>::value>::type> {
 public:
  static Status Serialize(const grpc::protobuf::Message& msg,
                          grpc_byte_buffer** bp, bool* own_buffer) {
    return internal::GenericSerialize<internal::GrpcBufferWriter, T>(
        msg, bp, own_buffer);
  }

  static Status Deserialize(grpc_byte_buffer* buffer,
                            grpc::protobuf::Message* msg) {
    return internal::GenericDeserialize<internal::GrpcBufferReader, T>(buffer,
                                                                       msg);
  }
};
#endif

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_PROTO_UTILS_H
