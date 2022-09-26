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

#include "src/core/ext/transport/binder/utils/ndk_binder.h"

#ifndef GRPC_NO_BINDER

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <dlfcn.h>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"

namespace {
void* GetNdkBinderHandle() {
  // TODO(mingcl): Consider using RTLD_NOLOAD to check if it is already loaded
  // first
  static void* handle = dlopen("libbinder_ndk.so", RTLD_LAZY);
  if (handle == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Cannot open libbinder_ndk.so. Does this device support API level 29?");
    GPR_ASSERT(0);
  }
  return handle;
}

JavaVM* g_jvm = nullptr;
grpc_core::Mutex g_jvm_mu;

// Whether the thread has already attached to JVM (this is to prevent
// repeated attachment in `AttachJvm()`)
thread_local bool g_is_jvm_attached = false;

void SetJvm(JNIEnv* env) {
  // OK to lock here since this function will only be called once for each
  // connection.
  grpc_core::MutexLock lock(&g_jvm_mu);
  if (g_jvm != nullptr) {
    return;
  }
  JavaVM* jvm = nullptr;
  jint error = env->GetJavaVM(&jvm);
  if (error != JNI_OK) {
    gpr_log(GPR_ERROR, "Failed to get JVM");
  }
  g_jvm = jvm;
  gpr_log(GPR_INFO, "JVM cached");
}

// `SetJvm` need to be called in the process before `AttachJvm`. This is always
// the case because one of `AIBinder_fromJavaBinder`/`AIBinder_toJavaBinder`
// will be called before we actually uses the binder. Return `false` if not able
// to attach to JVM. Return `true` if JVM is attached (or already attached).
bool AttachJvm() {
  if (g_is_jvm_attached) {
    return true;
  }
  // Note: The following code would be run at most once per thread.
  grpc_core::MutexLock lock(&g_jvm_mu);
  if (g_jvm == nullptr) {
    gpr_log(GPR_ERROR, "JVM not cached yet");
    return false;
  }
  JNIEnv* env_unused;
  // Note that attach a thread that is already attached is a no-op, so it is
  // fine to call this again if the thread has already been attached by other.
  g_jvm->AttachCurrentThread(&env_unused, /* thr_args= */ nullptr);
  gpr_log(GPR_INFO, "JVM attached successfully");
  g_is_jvm_attached = true;
  return true;
}

}  // namespace

namespace grpc_binder {
namespace ndk_util {

// Helper macro to obtain the function pointer corresponding to the name
#define FORWARD(name)                                                  \
  typedef decltype(&name) func_type;                                   \
  static func_type ptr =                                               \
      reinterpret_cast<func_type>(dlsym(GetNdkBinderHandle(), #name)); \
  if (ptr == nullptr) {                                                \
    gpr_log(GPR_ERROR,                                                 \
            "dlsym failed. Cannot find %s in libbinder_ndk.so. "       \
            "BinderTransport requires API level >= 33",                \
            #name);                                                    \
    GPR_ASSERT(0);                                                     \
  }                                                                    \
  return ptr

void AIBinder_Class_disableInterfaceTokenHeader(AIBinder_Class* clazz) {
  FORWARD(AIBinder_Class_disableInterfaceTokenHeader)(clazz);
}

void* AIBinder_getUserData(AIBinder* binder) {
  FORWARD(AIBinder_getUserData)(binder);
}

uid_t AIBinder_getCallingUid() { FORWARD(AIBinder_getCallingUid)(); }

AIBinder* AIBinder_fromJavaBinder(JNIEnv* env, jobject binder) {
  SetJvm(env);
  FORWARD(AIBinder_fromJavaBinder)(env, binder);
}

AIBinder_Class* AIBinder_Class_define(const char* interfaceDescriptor,
                                      AIBinder_Class_onCreate onCreate,
                                      AIBinder_Class_onDestroy onDestroy,
                                      AIBinder_Class_onTransact onTransact) {
  FORWARD(AIBinder_Class_define)
  (interfaceDescriptor, onCreate, onDestroy, onTransact);
}

AIBinder* AIBinder_new(const AIBinder_Class* clazz, void* args) {
  FORWARD(AIBinder_new)(clazz, args);
}

bool AIBinder_associateClass(AIBinder* binder, const AIBinder_Class* clazz) {
  FORWARD(AIBinder_associateClass)(binder, clazz);
}

void AIBinder_incStrong(AIBinder* binder) {
  FORWARD(AIBinder_incStrong)(binder);
}

void AIBinder_decStrong(AIBinder* binder) {
  FORWARD(AIBinder_decStrong)(binder);
}

binder_status_t AIBinder_transact(AIBinder* binder, transaction_code_t code,
                                  AParcel** in, AParcel** out,
                                  binder_flags_t flags) {
  if (!AttachJvm()) {
    gpr_log(GPR_ERROR, "failed to attach JVM. AIBinder_transact might fail.");
  }
  FORWARD(AIBinder_transact)(binder, code, in, out, flags);
}

binder_status_t AParcel_readByteArray(const AParcel* parcel, void* arrayData,
                                      AParcel_byteArrayAllocator allocator) {
  FORWARD(AParcel_readByteArray)(parcel, arrayData, allocator);
}

void AParcel_delete(AParcel* parcel) { FORWARD(AParcel_delete)(parcel); }
int32_t AParcel_getDataSize(const AParcel* parcel) {
  FORWARD(AParcel_getDataSize)(parcel);
}

binder_status_t AParcel_writeInt32(AParcel* parcel, int32_t value) {
  FORWARD(AParcel_writeInt32)(parcel, value);
}

binder_status_t AParcel_writeInt64(AParcel* parcel, int64_t value) {
  FORWARD(AParcel_writeInt64)(parcel, value);
}

binder_status_t AParcel_writeStrongBinder(AParcel* parcel, AIBinder* binder) {
  FORWARD(AParcel_writeStrongBinder)(parcel, binder);
}

binder_status_t AParcel_writeString(AParcel* parcel, const char* string,
                                    int32_t length) {
  FORWARD(AParcel_writeString)(parcel, string, length);
}

binder_status_t AParcel_readInt32(const AParcel* parcel, int32_t* value) {
  FORWARD(AParcel_readInt32)(parcel, value);
}

binder_status_t AParcel_readInt64(const AParcel* parcel, int64_t* value) {
  FORWARD(AParcel_readInt64)(parcel, value);
}

binder_status_t AParcel_readString(const AParcel* parcel, void* stringData,
                                   AParcel_stringAllocator allocator) {
  FORWARD(AParcel_readString)(parcel, stringData, allocator);
}

binder_status_t AParcel_readStrongBinder(const AParcel* parcel,
                                         AIBinder** binder) {
  FORWARD(AParcel_readStrongBinder)(parcel, binder);
}

binder_status_t AParcel_writeByteArray(AParcel* parcel, const int8_t* arrayData,
                                       int32_t length) {
  FORWARD(AParcel_writeByteArray)(parcel, arrayData, length);
}

binder_status_t AIBinder_prepareTransaction(AIBinder* binder, AParcel** in) {
  FORWARD(AIBinder_prepareTransaction)(binder, in);
}

jobject AIBinder_toJavaBinder(JNIEnv* env, AIBinder* binder) {
  SetJvm(env);
  FORWARD(AIBinder_toJavaBinder)(env, binder);
}

}  // namespace ndk_util
}  // namespace grpc_binder

#endif
#endif
