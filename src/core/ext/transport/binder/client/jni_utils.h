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

#include <grpc/impl/codegen/port_platform.h>

#include <jni.h>

#include <string>

// TODO(mingcl): Put these functions in a proper namespace
// TODO(mingcl): Use string_view
void CallStaticJavaMethod(JNIEnv* env, const std::string& clazz,
                          const std::string& method, const std::string& type,
                          jobject application);

jobject CallStaticJavaMethodForObject(JNIEnv* env, const std::string& clazz,
                                      const std::string& method,
                                      const std::string& type);

#endif

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_JNI_UTILS_H
