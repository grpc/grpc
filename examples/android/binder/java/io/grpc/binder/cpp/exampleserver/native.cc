// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_ibinder_jni.h>
#include <android/binder_interface_utils.h>
#include <android/log.h>
#include <jni.h>

#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/helloworld.pb.h"

#include <grpcpp/grpcpp.h>

#include "src/core/ext/transport/binder/security_policy/untrusted_security_policy.h"
#include "src/core/ext/transport/binder/server/binder_server.h"
#include "src/core/ext/transport/binder/server/binder_server_credentials.h"

namespace {
class GreeterService : public helloworld::Greeter::Service {
 public:
  grpc::Status SayHello(grpc::ServerContext*,
                        const helloworld::HelloRequest* request,
                        helloworld::HelloReply* response) override {
    __android_log_print(ANDROID_LOG_INFO, "DemoServer", "Line number %d",
                        __LINE__);
    __android_log_print(ANDROID_LOG_INFO, "DemoServer", "Got hello request: %s",
                        request->name().c_str());
    response->set_message("Hi, " + request->name());
    return grpc::Status::OK;
  }
};

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_io_grpc_binder_cpp_exampleserver_ExportedEndpointService_init_1grpc_1server(
    JNIEnv* env, jobject /*this*/) {
  __android_log_print(ANDROID_LOG_INFO, "DemoServer", "Line number %d",
                      __LINE__);
  static std::unique_ptr<grpc::Server> server = nullptr;

  if (server != nullptr) {
    // Already initiated
    return;
  }

  static GreeterService service;
  grpc::ServerBuilder server_builder;
  server_builder.RegisterService(&service);

  // TODO(mingcl): Use same signature security after it become available
  server_builder.AddListeningPort(
      "binder:example.service",
      grpc::experimental::BinderServerCredentials(
          std::make_shared<
              grpc::experimental::binder::UntrustedSecurityPolicy>()));

  server = server_builder.BuildAndStart();
}
