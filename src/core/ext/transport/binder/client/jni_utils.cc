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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/client/jni_utils.h"

#include <grpc/support/log.h>

#if defined(ANDROID) || defined(__ANDROID__)

namespace grpc_binder {

void CallStaticJavaMethod(JNIEnv* env, const std::string& clazz,
                          const std::string& method, const std::string& type,
                          jobject application, const std::string& pkg,
                          const std::string& cls) {
  jclass cl = env->FindClass(clazz.c_str());
  if (cl == nullptr) {
    gpr_log(GPR_ERROR, "No class %s", clazz.c_str());
  }

  jmethodID mid = env->GetStaticMethodID(cl, method.c_str(), type.c_str());
  if (mid == nullptr) {
    gpr_log(GPR_ERROR, "No method id %s", method.c_str());
  }

  env->CallStaticVoidMethod(cl, mid, application,
                            env->NewStringUTF(pkg.c_str()),
                            env->NewStringUTF(cls.c_str()));
}

void CallStaticJavaMethod(JNIEnv* env, const std::string& clazz,
                          const std::string& method, const std::string& type,
                          jobject application, const std::string& pkg,
                          const std::string& cls, const std::string& conn_id) {
  jclass cl = env->FindClass(clazz.c_str());
  if (cl == nullptr) {
    gpr_log(GPR_ERROR, "No class %s", clazz.c_str());
  }

  jmethodID mid = env->GetStaticMethodID(cl, method.c_str(), type.c_str());
  if (mid == nullptr) {
    gpr_log(GPR_ERROR, "No method id %s", method.c_str());
  }

  env->CallStaticVoidMethod(
      cl, mid, application, env->NewStringUTF(pkg.c_str()),
      env->NewStringUTF(cls.c_str()), env->NewStringUTF(conn_id.c_str()));
}

jobject CallStaticJavaMethodForObject(JNIEnv* env, const std::string& clazz,
                                      const std::string& method,
                                      const std::string& type) {
  jclass cl = env->FindClass(clazz.c_str());
  if (cl == nullptr) {
    gpr_log(GPR_ERROR, "No class %s", clazz.c_str());
    return nullptr;
  }

  jmethodID mid = env->GetStaticMethodID(cl, method.c_str(), type.c_str());
  if (mid == nullptr) {
    gpr_log(GPR_ERROR, "No method id %s", method.c_str());
    return nullptr;
  }

  jobject object = env->CallStaticObjectMethod(cl, mid);
  if (object == nullptr) {
    gpr_log(GPR_ERROR, "Got null object from Java");
    return nullptr;
  }

  return object;
}

}  // namespace grpc_binder

#endif
