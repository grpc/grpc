/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <atomic>

#include <grpc++/grpc++.h>
#include <jni.h>

#include "helloworld.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

std::atomic<bool> stop_server(false);

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
};

void StartServer(JNIEnv* env, jobject obj, jmethodID is_cancelled_mid,
                 int port) {
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "0.0.0.0:%d", port);

  GreeterServiceImpl service;
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(host_port, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  while (!stop_server.load()) {
    // Check with the Java code to see if the user has requested the server stop or the app is no
    // longer in the foreground.
    jboolean is_cancelled = env->CallBooleanMethod(obj, is_cancelled_mid);
    if (is_cancelled == JNI_TRUE) {
      stop_server = true;
    }
  }
}

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string SayHello(const std::string& user) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(user);

    // Container for the data we expect from the server.
    HelloReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;
    // The actual RPC.
    Status status = stub_->SayHello(&context, request, &reply);

    if (status.ok()) {
      return reply.message();
    } else {
      return status.error_message();
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

// Send an RPC and return the response. Invoked from Java code.
extern "C" JNIEXPORT jstring JNICALL
Java_io_grpc_helloworldexample_cpp_HelloworldActivity_sayHello(
    JNIEnv* env, jobject obj_unused, jstring host_raw, jint port_raw,
    jstring message_raw) {
  const char* host_chars = env->GetStringUTFChars(host_raw, (jboolean*)0);
  std::string host(host_chars, env->GetStringUTFLength(host_raw));

  int port = static_cast<int>(port_raw);

  const char* message_chars = env->GetStringUTFChars(message_raw, (jboolean*)0);
  std::string message(message_chars, env->GetStringUTFLength(message_raw));

  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", host.c_str(), port);

  GreeterClient greeter(
      grpc::CreateChannel(host_port, grpc::InsecureChannelCredentials()));
  std::string reply = greeter.SayHello(message);

  return env->NewStringUTF(reply.c_str());
}

// Start the server. Invoked from Java code.
extern "C" JNIEXPORT void JNICALL
Java_io_grpc_helloworldexample_cpp_HelloworldActivity_startServer(
    JNIEnv* env, jobject obj_this, jint port_raw) {
  int port = static_cast<int>(port_raw);

  jclass cls = env->GetObjectClass(obj_this);
  jmethodID is_cancelled_mid =
      env->GetMethodID(cls, "isRunServerTaskCancelled", "()Z");

  stop_server = false;

  StartServer(env, obj_this, is_cancelled_mid, port);
}
