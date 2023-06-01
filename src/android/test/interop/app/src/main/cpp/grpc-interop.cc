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

#include <jni.h>

#include "absl/strings/str_format.h"

#include <grpcpp/grpcpp.h>

#include "test/cpp/interop/interop_client.h"

std::shared_ptr<grpc::testing::InteropClient> GetClient(const char* host,
                                                        int port,
                                                        bool use_tls) {
  std::shared_ptr<grpc::ChannelCredentials> credentials;
  if (use_tls) {
    credentials = grpc::SslCredentials(grpc::SslCredentialsOptions());
  } else {
    credentials = grpc::InsecureChannelCredentials();
  }
  std::string host_port = absl::StrFormat("%s:%d", host, port);
  return std::make_shared<grpc::testing::InteropClient>(
      [host_port, credentials](grpc::ChannelArguments args) {
        return grpc::CreateCustomChannel(host_port, credentials, args);
      },
      true, false);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doEmpty(JNIEnv* env, jobject obj_this,
                                                 jstring host_raw,
                                                 jint port_raw,
                                                 jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  jboolean result = GetClient(host, port, use_tls)->DoEmpty();
  env->ReleaseStringUTFChars(host_raw, host);
  return result;
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

  jboolean result = GetClient(host, port, use_tls)->DoLargeUnary();
  env->ReleaseStringUTFChars(host_raw, host);
  return result;
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

  jboolean result = GetClient(host, port, use_tls)->DoEmptyStream();
  env->ReleaseStringUTFChars(host_raw, host);
  return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doRequestStreaming(
    JNIEnv* env, jobject obj_this, jstring host_raw, jint port_raw,
    jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  jboolean result = GetClient(host, port, use_tls)->DoRequestStreaming();
  env->ReleaseStringUTFChars(host_raw, host);
  return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_grpc_interop_cpp_InteropActivity_doResponseStreaming(
    JNIEnv* env, jobject obj_this, jstring host_raw, jint port_raw,
    jboolean use_tls_raw) {
  const char* host = env->GetStringUTFChars(host_raw, (jboolean*)0);
  int port = static_cast<int>(port_raw);
  bool use_tls = static_cast<bool>(use_tls_raw);

  jboolean result = GetClient(host, port, use_tls)->DoResponseStreaming();
  env->ReleaseStringUTFChars(host_raw, host);
  return result;
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

  jboolean result = GetClient(host, port, use_tls)->DoPingPong();
  env->ReleaseStringUTFChars(host_raw, host);
  return result;
}
