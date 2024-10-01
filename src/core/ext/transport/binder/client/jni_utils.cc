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

#include "src/core/ext/transport/binder/client/jni_utils.h"

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/support/port_platform.h>

#ifndef GRPC_NO_BINDER

#include "src/core/util/crash.h"

#if defined(ANDROID) || defined(__ANDROID__)

namespace grpc_binder {

jclass FindNativeConnectionHelper(JNIEnv* env) {
  return FindNativeConnectionHelper(
      env, [env](std::string cl) { return env->FindClass(cl.c_str()); });
}

jclass FindNativeConnectionHelper(
    JNIEnv* env, std::function<void*(std::string)> class_finder) {
  auto do_find = [env, class_finder]() {
    jclass cl = static_cast<jclass>(
        class_finder("io/grpc/binder/cpp/NativeConnectionHelper"));
    if (cl == nullptr) {
      return cl;
    }
    jclass global_cl = static_cast<jclass>(env->NewGlobalRef(cl));
    env->DeleteLocalRef(cl);
    CHECK_NE(global_cl, nullptr);
    return global_cl;
  };
  static jclass connection_helper_class = do_find();
  if (connection_helper_class != nullptr) {
    return connection_helper_class;
  }
  // Some possible reasons:
  //   * There is no Java class in the call stack and this is not invoked
  //   from JNI_OnLoad
  //   * The APK does not correctly depends on the helper class, or the
  //   class get shrinked
  LOG(ERROR)
      << "Cannot find binder transport Java helper class. Did you invoke "
         "grpc::experimental::InitializeBinderChannelJavaClass correctly "
         "beforehand? Did the APK correctly include the connection helper "
         "class (i.e depends on build target "
         "src/core/ext/transport/binder/java/io/grpc/binder/"
         "cpp:connection_helper) ?";
  // TODO(mingcl): Maybe it is worth to try again so the failure can be fixed
  // by invoking this function again at a different thread.
  return nullptr;
}

void TryEstablishConnection(JNIEnv* env, jobject application,
                            absl::string_view pkg, absl::string_view cls,
                            absl::string_view action_name,
                            absl::string_view conn_id) {
  std::string method = "tryEstablishConnection";
  std::string type =
      "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/"
      "lang/String;Ljava/lang/String;)V";

  jclass cl = FindNativeConnectionHelper(env);
  if (cl == nullptr) {
    return;
  }

  jmethodID mid = env->GetStaticMethodID(cl, method.c_str(), type.c_str());
  if (mid == nullptr) {
    LOG(ERROR) << "No method id " << method;
  }

  env->CallStaticVoidMethod(cl, mid, application,
                            env->NewStringUTF(std::string(pkg).c_str()),
                            env->NewStringUTF(std::string(cls).c_str()),
                            env->NewStringUTF(std::string(action_name).c_str()),
                            env->NewStringUTF(std::string(conn_id).c_str()));
}

void TryEstablishConnectionWithUri(JNIEnv* env, jobject application,
                                   absl::string_view uri,
                                   absl::string_view conn_id) {
  std::string method = "tryEstablishConnectionWithUri";
  std::string type =
      "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V";

  jclass cl = FindNativeConnectionHelper(env);
  if (cl == nullptr) {
    return;
  }

  jmethodID mid = env->GetStaticMethodID(cl, method.c_str(), type.c_str());
  if (mid == nullptr) {
    LOG(ERROR) << "No method id " << method;
  }

  env->CallStaticVoidMethod(cl, mid, application,
                            env->NewStringUTF(std::string(uri).c_str()),
                            env->NewStringUTF(std::string(conn_id).c_str()));
}

bool IsSignatureMatch(JNIEnv* env, jobject context, int uid1, int uid2) {
  const std::string method = "isSignatureMatch";
  const std::string type = "(Landroid/content/Context;II)Z";

  jclass cl = FindNativeConnectionHelper(env);
  if (cl == nullptr) {
    return false;
  }

  jmethodID mid = env->GetStaticMethodID(cl, method.c_str(), type.c_str());
  if (mid == nullptr) {
    LOG(ERROR) << "No method id " << method;
  }

  jboolean result = env->CallStaticBooleanMethod(cl, mid, context, uid1, uid2);
  return result == JNI_TRUE;
}

}  // namespace grpc_binder

#endif
#endif
