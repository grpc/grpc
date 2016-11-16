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

#ifndef GRPCXX_IMPL_CODEGEN_THRIFT_SERIALIZER_H
#define GRPCXX_IMPL_CODEGEN_THRIFT_SERIALIZER_H

#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/slice.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/protocol/TCompactProtocol.h>
#include <thrift/protocol/TProtocolException.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportUtils.h>
#include <boost/make_shared.hpp>
#include <memory>
#include <stdexcept>
#include <string>

namespace apache {
namespace thrift {
namespace util {

using apache::thrift::protocol::TBinaryProtocolT;
using apache::thrift::protocol::TCompactProtocolT;
using apache::thrift::protocol::TMessageType;
using apache::thrift::protocol::TNetworkBigEndian;
using apache::thrift::transport::TMemoryBuffer;
using apache::thrift::transport::TBufferBase;
using apache::thrift::transport::TTransport;

template <typename Dummy, typename Protocol>
class ThriftSerializer {
 public:
  ThriftSerializer()
      : prepared_(false),
        last_deserialized_(false),
        serialize_version_(false) {}

  virtual ~ThriftSerializer() {}

  // Serialize the passed type into the internal buffer
  // and returns a pointer to internal buffer and its size
  template <typename T>
  void Serialize(const T& fields, const uint8_t** serialized_buffer,
                 size_t* serialized_len) {
    // prepare or reset buffer
    if (!prepared_ || last_deserialized_) {
      prepare();
    } else {
      buffer_->resetBuffer();
    }
    last_deserialized_ = false;

    // if required serialize protocol version
    if (serialize_version_) {
      protocol_->writeMessageBegin("", TMessageType(0), 0);
    }

    // serialize fields into buffer
    fields.write(protocol_.get());

    // write the end of message
    if (serialize_version_) {
      protocol_->writeMessageEnd();
    }

    uint8_t* byte_buffer;
    uint32_t byte_buffer_size;
    buffer_->getBuffer(&byte_buffer, &byte_buffer_size);
    *serialized_buffer = byte_buffer;
    *serialized_len = byte_buffer_size;
  }

  // Serialize the passed type into the byte buffer
  template <typename T>
  void Serialize(const T& fields, grpc_byte_buffer** bp) {
    const uint8_t* byte_buffer;
    size_t byte_buffer_size;

    Serialize(fields, &byte_buffer, &byte_buffer_size);

    grpc_slice slice = grpc_slice_from_copied_buffer(
        reinterpret_cast<const char*>(byte_buffer), byte_buffer_size);

    *bp = grpc_raw_byte_buffer_create(&slice, 1);

    grpc_slice_unref(slice);
  }

  // Deserialize the passed char array into  the passed type, returns the number
  // of bytes that have been consumed from the passed string.
  template <typename T>
  uint32_t Deserialize(uint8_t* serialized_buffer, size_t length, T* fields) {
    // prepare buffer if necessary
    if (!prepared_) {
      prepare();
    }
    last_deserialized_ = true;

    // reset buffer transport
    buffer_->resetBuffer(serialized_buffer, length);

    // read the protocol version if necessary
    if (serialize_version_) {
      std::string name = "";
      TMessageType mt = static_cast<TMessageType>(0);
      int32_t seq_id = 0;
      protocol_->readMessageBegin(name, mt, seq_id);
    }

    // deserialize buffer into fields
    uint32_t len = fields->read(protocol_.get());

    // read the end of message
    if (serialize_version_) {
      protocol_->readMessageEnd();
    }

    return len;
  }

  // Deserialize the passed byte buffer to passed type, returns the number
  // of bytes consumed from byte buffer
  template <typename T>
  uint32_t Deserialize(grpc_byte_buffer* buffer, T* msg) {
    grpc_byte_buffer_reader reader;
    grpc_byte_buffer_reader_init(&reader, buffer);

    grpc_slice slice = grpc_byte_buffer_reader_readall(&reader);

    uint32_t len =
        Deserialize(GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice), msg);

    grpc_slice_unref(slice);

    grpc_byte_buffer_reader_destroy(&reader);

    return len;
  }

  // set serialization version flag
  void SetSerializeVersion(bool value) { serialize_version_ = value; }

  // Set the container size limit to deserialize
  // This function should be called after buffer_ is initialized
  void SetContainerSizeLimit(int32_t container_limit) {
    if (!prepared_) {
      prepare();
    }
    protocol_->setContainerSizeLimit(container_limit);
  }

  // Set the string size limit to deserialize
  // This function should be called after buffer_ is initialized
  void SetStringSizeLimit(int32_t string_limit) {
    if (!prepared_) {
      prepare();
    }
    protocol_->setStringSizeLimit(string_limit);
  }

 private:
  bool prepared_;
  bool last_deserialized_;
  boost::shared_ptr<TMemoryBuffer> buffer_;
  std::shared_ptr<Protocol> protocol_;
  bool serialize_version_;

  void prepare() {
    buffer_ = boost::make_shared<TMemoryBuffer>();
    // create a protocol for the memory buffer transport
    protocol_ = std::make_shared<Protocol>(buffer_);
    prepared_ = true;
  }

};  // ThriftSerializer

typedef ThriftSerializer<void, TBinaryProtocolT<TBufferBase, TNetworkBigEndian>>
    ThriftSerializerBinary;
typedef ThriftSerializer<void, TCompactProtocolT<TBufferBase>>
    ThriftSerializerCompact;

}  // namespace util
}  // namespace thrift
}  // namespace apache

#endif  // GRPCXX_IMPL_CODEGEN_THRIFT_SERIALIZER_H
