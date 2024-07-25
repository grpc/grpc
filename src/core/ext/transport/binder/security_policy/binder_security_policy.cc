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

#ifndef GRPC_NO_BINDER

#include <grpcpp/security/binder_security_policy.h>

#ifdef GPR_ANDROID

#include <jni.h>
#include <unistd.h>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "src/core/ext/transport/binder/client/jni_utils.h"
#include "src/core/lib/gprpp/crash.h"

#endif

namespace grpc {
namespace experimental {
namespace binder {

UntrustedSecurityPolicy::UntrustedSecurityPolicy() = default;

UntrustedSecurityPolicy::~UntrustedSecurityPolicy() = default;

bool UntrustedSecurityPolicy::IsAuthorized(int) { return true; };

InternalOnlySecurityPolicy::InternalOnlySecurityPolicy() = default;

InternalOnlySecurityPolicy::~InternalOnlySecurityPolicy() = default;

#ifdef GPR_ANDROID
bool InternalOnlySecurityPolicy::IsAuthorized(int uid) {
  return static_cast<uid_t>(uid) == getuid();
}
#else
bool InternalOnlySecurityPolicy::IsAuthorized(int) { return false; }
#endif

#ifdef GPR_ANDROID

namespace {
JNIEnv* GetEnv(JavaVM* vm) {
  if (vm == nullptr) return nullptr;

  JNIEnv* result = nullptr;
  jint attach = vm->AttachCurrentThread(&result, nullptr);

  CHECK(JNI_OK == attach);
  CHECK_NE(result, nullptr);
  return result;
}
}  // namespace

SameSignatureSecurityPolicy::SameSignatureSecurityPolicy(JavaVM* jvm,
                                                         jobject context)
    : jvm_(jvm) {
  CHECK_NE(jvm, nullptr);
  CHECK_NE(context, nullptr);

  JNIEnv* env = GetEnv(jvm_);

  // Make sure the context is still valid when IsAuthorized() is called
  context_ = env->NewGlobalRef(context);
  CHECK_NE(context_, nullptr);
}

SameSignatureSecurityPolicy::~SameSignatureSecurityPolicy() {
  JNIEnv* env = GetEnv(jvm_);
  env->DeleteLocalRef(context_);
}

bool SameSignatureSecurityPolicy::IsAuthorized(int uid) {
  JNIEnv* env = GetEnv(jvm_);
  bool result = grpc_binder::IsSignatureMatch(env, context_, getuid(), uid);
  if (result) {
    LOG(INFO) << "uid " << getuid() << " and uid " << uid
              << " passed SameSignature check";
  } else {
    LOG(ERROR) << "uid " << getuid() << " and uid " << uid
               << " failed SameSignature check";
  }
  return result;
}

#endif

}  // namespace binder
}  // namespace experimental
}  // namespace grpc
#endif
