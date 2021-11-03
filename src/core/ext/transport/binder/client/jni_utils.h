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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_JNI_UTILS_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_JNI_UTILS_H

#if defined(ANDROID) || defined(__ANDROID__)

#include <grpc/support/port_platform.h>

#include <jni.h>

#include <string>

namespace grpc_binder {

// For now we hard code the arguments of the Java function because this is only
// used to call that single function.
void CallStaticJavaMethod(JNIEnv* env, const std::string& clazz,
                          const std::string& method, const std::string& type,
                          jobject application, const std::string& pkg,
                          const std::string& cls);
void CallStaticJavaMethod(JNIEnv* env, const std::string& clazz,
                          const std::string& method, const std::string& type,
                          jobject application, const std::string& pkg,
                          const std::string& cls, const std::string& conn_id);

jobject CallStaticJavaMethodForObject(JNIEnv* env, const std::string& clazz,
                                      const std::string& method,
                                      const std::string& type);
}  // namespace grpc_binder

#endif

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_JNI_UTILS_H
