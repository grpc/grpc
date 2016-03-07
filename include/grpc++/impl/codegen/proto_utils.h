/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include <grpc/impl/codegen/byte_buffer.h>
#include <grpc/impl/codegen/log.h>
#include <grpc++/impl/codegen/serialization_traits.h>
#include <grpc++/impl/codegen/config_protobuf.h>
#include <grpc++/impl/codegen/status.h>

namespace grpc {

class ProtoSerializerInterface {
 public:
  // Serialize the msg into a buffer created inside the function. The caller
  // should destroy the returned buffer when done with it. If serialization
  // fails,
  // false is returned and buffer is left unchanged.
  virtual Status SerializeProto(const grpc::protobuf::Message& msg,
                                grpc_byte_buffer** buffer) = 0;

  // The caller keeps ownership of buffer and msg.
  virtual Status DeserializeProto(grpc_byte_buffer* buffer,
                                  grpc::protobuf::Message* msg,
                                  int max_message_size) = 0;
};

// TODO(dgq): This is a temporary fix to work around codegen issues. Its purpose
// is to hold a polymorphic proto serializer/deserializer instance. It's
// initialized as part of src/cpp/proto/proto_serializer.cc.
//
// This global variable plus all related code (ProtoSerializerInteface,
// ProtoSerializer) will be removed in the future.
extern ProtoSerializerInterface* g_proto_serializer;

class ProtoSerializer : public ProtoSerializerInterface {
 public:
  // Serialize the msg into a buffer created inside the function. The caller
  // should destroy the returned buffer when done with it. If serialization
  // fails,
  // false is returned and buffer is left unchanged.
  Status SerializeProto(const grpc::protobuf::Message& msg,
                        grpc_byte_buffer** buffer) override;

  // The caller keeps ownership of buffer and msg.
  Status DeserializeProto(grpc_byte_buffer* buffer,
                          grpc::protobuf::Message* msg,
                          int max_message_size) override;
};

template <class T>
class SerializationTraits<T, typename std::enable_if<std::is_base_of<
                                 grpc::protobuf::Message, T>::value>::type> {
 public:
  static Status Serialize(const grpc::protobuf::Message& msg,
                          grpc_byte_buffer** buffer, bool* own_buffer) {
    *own_buffer = true;
    GPR_ASSERT(g_proto_serializer != nullptr &&
               "No ProtoSerializer instance registered. Make sure grpc++ is "
               "being initialized.");
    return g_proto_serializer->SerializeProto(msg, buffer);
  }
  static Status Deserialize(grpc_byte_buffer* buffer,
                            grpc::protobuf::Message* msg,
                            int max_message_size) {
    GPR_ASSERT(g_proto_serializer != nullptr &&
               "No ProtoSerializer instance registered. Make sure grpc++ is "
               "being initialized.");
    auto status =
        g_proto_serializer->DeserializeProto(buffer, msg, max_message_size);
    grpc_byte_buffer_destroy(buffer);
    return status;
  }
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_PROTO_UTILS_H
