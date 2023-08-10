#ifndef GRPC_TEST_CPP_UTIL_BYTE_BUFFER_PROTO_HELPER_H
#define GRPC_TEST_CPP_UTIL_BYTE_BUFFER_PROTO_HELPER_H

#include <memory>

#include <grpcpp/impl/codegen/config_protobuf.h>
#include <grpcpp/support/byte_buffer.h>

using namespace grpc;

namespace proto_helper {

bool ParseFromByteBuffer(ByteBuffer* buffer, grpc::protobuf::Message* message);

std::unique_ptr<ByteBuffer> SerializeToByteBuffer(std::string& message);

}  // namespace helper

#endif
