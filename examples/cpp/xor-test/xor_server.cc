#include <iostream>
#include <memory>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#ifdef BAZEL_BUILD
#include "examples/protos/cal_xor.grpc.pb.h"
#else
#include "cal_xor.grpc.pb.h"
#endif

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using XorJob::CalXor;
using XorJob::CalXorRequest;
using XorJob::CalXorResponse;

class XorServiceImpl final : public CalXor::Service {
  Status CalculateXor(ServerContext* context, 
                      const CalXorRequest* request,
                      CalXorResponse* reply) override {
    int64_t num1 = request->num1();
    int64_t num2 = request->num2();
    int64_t num3 = num1^num2;
    reply->set_num(num3);
    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("0.0.0.0:50051");
  XorServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;

  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  server->Wait();
}

int main(int argc, char** argv) {
  RunServer();
  return 0;
}
