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
#include <jni.h>

#include <grpcpp/grpcpp.h>

#include "src/core/ext/transport/binder/server/binder_server.h"
#include "test/cpp/microbenchmarks/bm_callback_ping_pong_binder.h"

grpc::testing::CallbackPingPongBinderServer* server = nullptr;

extern "C" JNIEXPORT void JNICALL
Java_io_grpc_binder_cpp_benchmarks_server_ExportedEndpointService_init_1grpc_1server(
    JNIEnv* env, jobject /*this*/) {
  if (server != nullptr) {
    return;
  }
  server = new grpc::testing::CallbackPingPongBinderServer(
      "binder://callback.ping.pong.benchmark");
}

extern "C" JNIEXPORT jobject JNICALL
Java_io_grpc_binder_cpp_benchmarks_server_ExportedEndpointService_get_1endpoint_1binder(
    JNIEnv* env, jobject /*this*/) {
  AIBinder* ai_binder =
      static_cast<AIBinder*>(grpc::experimental::binder::GetEndpointBinder(
          "callback.ping.pong.benchmark"));
  return AIBinder_toJavaBinder(env, ai_binder);
}
