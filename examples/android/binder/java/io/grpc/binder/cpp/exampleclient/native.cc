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
#include <grpcpp/security/binder_security_policy.h>

extern "C" JNIEXPORT jstring JNICALL
Java_io_grpc_binder_cpp_exampleclient_ButtonPressHandler_native_1entry(
    JNIEnv* env, jobject /*this*/, jobject application) {
  // Lower the gRPC logging level, here it is just for demo and debugging
  // purpose.
  setenv("GRPC_VERBOSITY", "INFO", true);
  if (grpc::experimental::InitializeBinderChannelJavaClass(env)) {
    __android_log_print(ANDROID_LOG_INFO, "DemoClient",
                        "InitializeBinderChannelJavaClass succeed");
  } else {
    __android_log_print(ANDROID_LOG_INFO, "DemoClient",
                        "InitializeBinderChannelJavaClass failed");
  }
  static bool first = true;
  static std::shared_ptr<grpc::Channel> channel;
  if (first) {
    first = false;
    JavaVM* jvm;
    {
      jint result = env->GetJavaVM(&jvm);
      assert(result == 0);
    }
    grpc::ChannelArguments ch_args;
    // This is not required since "grpc.io.action.BIND" is already the default.
    ch_args.SetString("grpc.binder.custom_android_intent_action_name",
                      "grpc.io.action.BIND");
    channel = grpc::experimental::CreateCustomBinderChannel(
        env, application, "io.grpc.binder.cpp.exampleserver",
        "io.grpc.binder.cpp.exampleserver.ExportedEndpointService",
        std::make_shared<
            grpc::experimental::binder::SameSignatureSecurityPolicy>(
            jvm, application),
        ch_args);
    return env->NewStringUTF("Clicked 1 time, channel created");
  } else {
    auto stub = helloworld::Greeter::NewStub(channel);
    grpc::ClientContext context;
    helloworld::HelloRequest request;
    helloworld::HelloReply response;
    request.set_name("BinderTransportClient");
    grpc::Status status = stub->SayHello(&context, request, &response);
    if (status.ok()) {
      return env->NewStringUTF(response.message().c_str());
    }
    return env->NewStringUTF(
        std::string("Clicked more than 1 time. Status not ok " +
                    std::to_string(status.error_code()))
            .c_str());
  }
}
