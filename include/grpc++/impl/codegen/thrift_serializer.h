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

#include <memory>
#include <string>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/protocol/TCompactProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportUtils.h>
#include <grpc/impl/codegen/byte_buffer.h>

namespace apache {
namespace thrift {
namespace util {

using apache::thrift::protocol::TBinaryProtocolT;
using apache::thrift::protocol::TCompactProtocolT;
using apache::thrift::protocol::TNetworkBigEndian;
using apache::thrift::transport::TMemoryBuffer;
using apache::thrift::transport::TBufferBase;
using apache::thrift::transport::TTransport;
using std::shared_ptr;


template <typename Dummy, typename P>
class ThriftSerializer {
public:
  ThriftSerializer()
  : prepared_ (false)
  , lastDeserialized_ (false)
  , serializeVersion_ (false) {}

  /**
   * Serialize the passed type into the internal buffer
   * and returns a pointer to internal buffer and its size
   *
   */
  template <typename T>
  void serialize(const T& fields, const uint8_t** serializedBuffer,
      size_t* serializedLen);

  /**
   * Serialize the passed type into the byte buffer
   */
  template <typename T>
  void serialize(const T& fields, grpc_byte_buffer** bp);

  /**
   * Deserialize the passed char array into  the passed type, returns the number
   * of bytes that have been consumed from the passed string.
   */
  template <typename T>
  uint32_t deserialize(const uint8_t* serializedBuffer, size_t length,
      T* fields);

  /**
   * Deserialize the passed byte buffer to passed type, returns the number
   * of bytes consumed from byte buffer
   */
  template <typename T>
  uint32_t deserialize(grpc_byte_buffer* buffer, T* msg);

  void setSerializeVersion(bool value);

  virtual ~ThriftSerializer() {}


  /**
   * Set the container size limit to deserialize
   * This function should be called after buffer_ is initialized
   */
  void setContainerSizeLimit(int32_t container_limit) {
    if (!prepared_) {
      prepare();
    }
    protocol_->setContainerSizeLimit(container_limit);
  }

  /**
   * Set the string size limit to deserialize
   * This function should be called after buffer_ is initialized
   */
  void setStringSizeLimit(int32_t string_limit) {
    if (!prepared_) {
      prepare();
    }
    protocol_->setStringSizeLimit(string_limit);
  }


  private:
    void prepare();

  private:
    typedef P Protocol;
    bool prepared_;
    bool lastDeserialized_;
    boost::shared_ptr<TMemoryBuffer> buffer_;
    shared_ptr<Protocol> protocol_;
    bool serializeVersion_;
}; // ThriftSerializer

template <typename Dummy = void>
struct ThriftSerializerBinary : public ThriftSerializer<Dummy, TBinaryProtocolT<TBufferBase, TNetworkBigEndian> > {};


template <typename Dummy = void>
struct ThriftSerializerCompact : public ThriftSerializer<Dummy, TCompactProtocolT<TBufferBase> >{ };

}}} // namespace apache::thrift::util

#include <grpc++/impl/codegen/thrift_serializer_inl.h>

#endif
