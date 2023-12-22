#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>

namespace generic_grpc 
{
void RPC(const std::string target_str, const std::string method,
         const grpc::ByteBuffer* send_buf);
}
