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

#include <android/log.h>
#include <jni.h>

#include "examples/protos/helloworld.grpc.pb.h"
#include "examples/protos/helloworld.pb.h"

#include <grpcpp/create_channel_binder.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/binder_credentials.h>
#include <grpcpp/security/binder_security_policy.h>

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
    JNIEnv* env, jobject /*this*/, jobject context) {
  // Lower the gRPC logging level, here it is just for demo and debugging
  // purpose.
  setenv("GRPC_VERBOSITY", "INFO", true);
  __android_log_print(ANDROID_LOG_INFO, "DemoServer", "Line number %d",
                      __LINE__);
  static std::unique_ptr<grpc::Server> server = nullptr;

  if (server != nullptr) {
    // Already initiated
    return;
  }

  if (grpc::experimental::InitializeBinderChannelJavaClass(env)) {
    __android_log_print(ANDROID_LOG_INFO, "DemoServer",
                        "InitializeBinderChannelJavaClass succeed");
  } else {
    __android_log_print(ANDROID_LOG_INFO, "DemoServer",
                        "InitializeBinderChannelJavaClass failed");
  }

  static GreeterService service;
  grpc::ServerBuilder server_builder;
  server_builder.RegisterService(&service);

  JavaVM* jvm;
  {
    jint result = env->GetJavaVM(&jvm);
    assert(result == 0);
  }
  server_builder.AddListeningPort(
      "binder:example.service",
      grpc::experimental::BinderServerCredentials(
          std::make_shared<
              grpc::experimental::binder::SameSignatureSecurityPolicy>(
              jvm, context)));

  server = server_builder.BuildAndStart();
}
