#pragma once

#ifndef OTHER_SERVICE_H 
#define OTHER_SERVICE_H

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

#include "IDynamicService.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic and data behind the server's behavior.
class OtherServiceImpl final : public Greeter::Service, public IDynamicService {
 public:
  Status SayHello(
    ServerContext* context,
    const HelloRequest* request,
    HelloReply* reply
  );
};

extern "C" {
  __declspec(dllexport) IDynamicService* CreateOtherServiceHelper();
}

#endif
