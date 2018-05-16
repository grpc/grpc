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

#include <grpcpp/grpcpp.h>
#include <jni.h>
#include <src/core/lib/gpr/env.h>

#include "test/cpp/interop/interop_client.h"

extern "C" JNIEXPORT void JNICALL
Java_io_grpc_interop_cpp_InteropActivity_configureSslRoots(JNIEnv* env,
                                                           jobject obj_this,
                                                           jstring path_raw) {
  const char* path = env->GetStringUTFChars(path_raw, (jboolean*)0);

  gpr_setenv("GRPC_DEFAULT_SSL_ROOTS_FILE_PATH", path);
}

std::shared_ptr<grpc::testing::InteropClient> GetClient(const char* host,
                                                        int port,
                                                        bool use_tls) {
  const int host_port_buf_size = 1024;
  char host_port[host_port_buf_size];
  snprintf(host_port, host_port_buf_size, "%s:%d", host, port);

  std::shared_ptr<grpc::ChannelCredentials> credentials;
  if (use_tls) {
    credentials = grpc::SslCredentials(grpc::SslCredentialsOptions());
  } else {
    credentials = grpc::InsecureChannelCredentials();
  }

  return std::shared_ptr<grpc::testing::InteropClient>(
      new grpc::testing::InteropClient(
          grpc::CreateChannel(host_port, credentials), true, false));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doEmpty(JNIEnv* env, jobject obj_this,
                                                 jstring host_raw,
                                                 jint port_raw,
                                                 jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  return GetClient(host, port, use_tls)->DoEmpty();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doLargeUnary(JNIEnv* env,
                                                      jobject obj_this,
                                                      jstring host_raw,
                                                      jint port_raw,
                                                      jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  return GetClient(host, port, use_tls)->DoLargeUnary();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doEmptyStream(JNIEnv* env,
                                                       jobject obj_this,
                                                       jstring host_raw,
                                                       jint port_raw,
                                                       jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  return GetClient(host, port, use_tls)->DoEmptyStream();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doRequestStreaming(
    JNIEnv* env, jobject obj_this, jstring host_raw, jint port_raw,
    jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  return GetClient(host, port, use_tls)->DoRequestStreaming();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doResponseStreaming(
    JNIEnv* env, jobject obj_this, jstring host_raw, jint port_raw,
    jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  return GetClient(host, port, use_tls)->DoResponseStreaming();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doPingPong(JNIEnv* env,
                                                    jobject obj_this,
                                                    jstring host_raw,
                                                    jint port_raw,
                                                    jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  return GetClient(host, port, use_tls)->DoPingPong();
}
