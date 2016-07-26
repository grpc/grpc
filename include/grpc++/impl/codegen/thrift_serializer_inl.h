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

 #ifndef GRPCXX_IMPL_CODEGEN_THRIFT_SERIALIZER_INL_H
 #define GRPCXX_IMPL_CODEGEN_THRIFT_SERIALIZER_INL_H

#include <stdexcept>
#include <string>
#include <grpc++/impl/codegen/thrift_serializer.h>
#include <grpc/impl/codegen/byte_buffer_reader.h>
#include <grpc/impl/codegen/slice.h>
#include <grpc/impl/codegen/slice_buffer.h>
#include <thrift/protocol/TProtocolException.h>

namespace apache {
namespace thrift {
namespace util {

using apache::thrift::protocol::TMessageType;

template <typename Dummy, typename P>
template <typename T>
void ThriftSerializer<Dummy, P>::serialize(const T& fields,
    const uint8_t** serializedBuffer, size_t* serializedLen) {

  // prepare or reset buffer
  if (!prepared_ || lastDeserialized_) {
    prepare();
  } else {
    buffer_->resetBuffer();
  }
  lastDeserialized_ = false;

  // if required serialize protocol version
  if (serializeVersion_) {
    protocol_->writeMessageBegin("", TMessageType(0), 0);
  }

  // serilaize fields into buffer
  fields.write(protocol_.get());

  // write the end of message
  if (serializeVersion_) {
    protocol_->writeMessageEnd();
  }

  // assign buffer to string
  uint8_t* byteBuffer;
  uint32_t byteBufferSize;
  buffer_->getBuffer(&byteBuffer, &byteBufferSize);
  *serializedBuffer = byteBuffer;
  *serializedLen = byteBufferSize; 
}

template <typename Dummy, typename P>
template <typename T>
void ThriftSerializer<Dummy, P>::serialize(const T& fields, grpc_byte_buffer** bp) {

  const uint8_t* byteBuffer;
  size_t byteBufferSize;
  serialize(fields, &byteBuffer, &byteBufferSize);

  gpr_slice slice = gpr_slice_from_copied_buffer((char*)byteBuffer,byteBufferSize);

  *bp = grpc_raw_byte_buffer_create(&slice, 1);

  gpr_slice_unref(slice);
}

template <typename Dummy, typename P>
template <typename T>
uint32_t ThriftSerializer<Dummy, P>::deserialize(const uint8_t* serializedBuffer,
    size_t length, T* fields) {
  // prepare buffer if necessary
  if (!prepared_) {
    prepare();
  }
  lastDeserialized_ = true;

  //reset buffer transport 
  buffer_->resetBuffer((uint8_t*)serializedBuffer, length);

  // read the protocol version if necessary
  if (serializeVersion_) {
    std::string name = "";
    TMessageType mt = (TMessageType) 0;
    int32_t seq_id = 0;
    protocol_->readMessageBegin(name, mt, seq_id);
  }

  // deserialize buffer into fields
  uint32_t len = fields->read(protocol_.get());

  // read the end of message
  if (serializeVersion_) {
    protocol_->readMessageEnd();
  }

  return len;
}

template <typename Dummy, typename P>
template <typename T>
uint32_t ThriftSerializer<Dummy, P>::deserialize(grpc_byte_buffer* bp, T* fields) {
  grpc_byte_buffer_reader reader;
  grpc_byte_buffer_reader_init(&reader, bp);

  gpr_slice slice = grpc_byte_buffer_reader_readall(&reader);

  uint32_t len = deserialize(GPR_SLICE_START_PTR(slice), GPR_SLICE_LENGTH(slice), fields);

  gpr_slice_unref(slice);

  grpc_byte_buffer_reader_destroy(&reader);

  return len;
}

template <typename Dummy, typename P>
void ThriftSerializer<Dummy, P>::setSerializeVersion(bool value) {
  serializeVersion_ = value;
}

template <typename Dummy, typename P>
void
ThriftSerializer<Dummy, P>::prepare()
{
  
  buffer_.reset(new TMemoryBuffer());

  // create a protocol for the memory buffer transport
  protocol_.reset(new Protocol(buffer_));

  prepared_ = true;
}

}}} // namespace apache::thrift::util

#endif
