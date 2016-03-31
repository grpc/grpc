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

#include "src/cpp/common/core_codegen.h"

#include <stdlib.h>

#include <grpc++/support/config.h>
#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/alloc.h>
#include <grpc/impl/codegen/byte_buffer.h>
#include <grpc/impl/codegen/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>

#include "src/core/lib/profiling/timers.h"

namespace {

const int kGrpcBufferWriterMaxBufferLength = 8192;

class GrpcBufferWriter GRPC_FINAL
    : public ::grpc::protobuf::io::ZeroCopyOutputStream {
 public:
  explicit GrpcBufferWriter(grpc_byte_buffer** bp, int block_size)
      : block_size_(block_size), byte_count_(0), have_backup_(false) {
    *bp = grpc_raw_byte_buffer_create(NULL, 0);
    slice_buffer_ = &(*bp)->data.raw.slice_buffer;
  }

  ~GrpcBufferWriter() GRPC_OVERRIDE {
    if (have_backup_) {
      gpr_slice_unref(backup_slice_);
    }
  }

  bool Next(void** data, int* size) GRPC_OVERRIDE {
    if (have_backup_) {
      slice_ = backup_slice_;
      have_backup_ = false;
    } else {
      slice_ = gpr_slice_malloc(block_size_);
    }
    *data = GPR_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_ASSERT(GPR_SLICE_LENGTH(slice_) <= INT_MAX);
    byte_count_ += * size = (int)GPR_SLICE_LENGTH(slice_);
    gpr_slice_buffer_add(slice_buffer_, slice_);
    return true;
  }

  void BackUp(int count) GRPC_OVERRIDE {
    gpr_slice_buffer_pop(slice_buffer_);
    if (count == block_size_) {
      backup_slice_ = slice_;
    } else {
      backup_slice_ =
          gpr_slice_split_tail(&slice_, GPR_SLICE_LENGTH(slice_) - count);
      gpr_slice_buffer_add(slice_buffer_, slice_);
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
    grpc_byte_buffer_reader_init(&reader_, buffer);
  }
  ~GrpcBufferReader() GRPC_OVERRIDE {
    grpc_byte_buffer_reader_destroy(&reader_);
  }

  bool Next(const void** data, int* size) GRPC_OVERRIDE {
    if (backup_count_ > 0) {
      *data = GPR_SLICE_START_PTR(slice_) + GPR_SLICE_LENGTH(slice_) -
              backup_count_;
      GPR_ASSERT(backup_count_ <= INT_MAX);
      *size = (int)backup_count_;
      backup_count_ = 0;
      return true;
    }
    if (!grpc_byte_buffer_reader_next(&reader_, &slice_)) {
      return false;
    }
    gpr_slice_unref(slice_);
    *data = GPR_SLICE_START_PTR(slice_);
    // On win x64, int is only 32bit
    GPR_ASSERT(GPR_SLICE_LENGTH(slice_) <= INT_MAX);
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
}  // namespace

namespace grpc {

grpc_completion_queue* CoreCodegen::grpc_completion_queue_create(
    void* reserved) {
  return ::grpc_completion_queue_create(reserved);
}

void CoreCodegen::grpc_completion_queue_destroy(grpc_completion_queue* cq) {
  ::grpc_completion_queue_destroy(cq);
}

grpc_event CoreCodegen::grpc_completion_queue_pluck(grpc_completion_queue* cq,
                                                    void* tag,
                                                    gpr_timespec deadline,
                                                    void* reserved) {
  return ::grpc_completion_queue_pluck(cq, tag, deadline, reserved);
}

void* CoreCodegen::gpr_malloc(size_t size) { return ::gpr_malloc(size); }

void CoreCodegen::gpr_free(void* p) { return ::gpr_free(p); }

void CoreCodegen::grpc_byte_buffer_destroy(grpc_byte_buffer* bb) {
  ::grpc_byte_buffer_destroy(bb);
}

void CoreCodegen::grpc_metadata_array_init(grpc_metadata_array* array) {
  ::grpc_metadata_array_init(array);
}

void CoreCodegen::grpc_metadata_array_destroy(grpc_metadata_array* array) {
  ::grpc_metadata_array_destroy(array);
}

gpr_timespec CoreCodegen::gpr_inf_future(gpr_clock_type type) {
  return ::gpr_inf_future(type);
}

void CoreCodegen::assert_fail(const char* failed_assertion) {
  gpr_log(GPR_ERROR, "assertion failed: %s", failed_assertion);
  abort();
}

Status CoreCodegen::SerializeProto(const grpc::protobuf::Message& msg,
                                   grpc_byte_buffer** bp) {
  GPR_TIMER_SCOPE("SerializeProto", 0);
  int byte_size = msg.ByteSize();
  if (byte_size <= kGrpcBufferWriterMaxBufferLength) {
    gpr_slice slice = gpr_slice_malloc(byte_size);
    GPR_ASSERT(GPR_SLICE_END_PTR(slice) ==
               msg.SerializeWithCachedSizesToArray(GPR_SLICE_START_PTR(slice)));
    *bp = grpc_raw_byte_buffer_create(&slice, 1);
    gpr_slice_unref(slice);
    return Status::OK;
  } else {
    GrpcBufferWriter writer(bp, kGrpcBufferWriterMaxBufferLength);
    return msg.SerializeToZeroCopyStream(&writer)
               ? Status::OK
               : Status(StatusCode::INTERNAL, "Failed to serialize message");
  }
}

Status CoreCodegen::DeserializeProto(grpc_byte_buffer* buffer,
                                     grpc::protobuf::Message* msg,
                                     int max_message_size) {
  GPR_TIMER_SCOPE("DeserializeProto", 0);
  if (buffer == nullptr) {
    return Status(StatusCode::INTERNAL, "No payload");
  }
  Status result = Status::OK;
  {
    GrpcBufferReader reader(buffer);
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
  grpc_byte_buffer_destroy(buffer);
  return result;
}

}  // namespace grpc
