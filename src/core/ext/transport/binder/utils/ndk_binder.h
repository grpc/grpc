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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_NDK_BINDER_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_NDK_BINDER_H

#include <grpc/support/port_platform.h>

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <assert.h>
#include <jni.h>

#include <memory>

// This file defines NdkBinder functions, variables, and types in
// ::grpc_binder::ndk_util namespace. This allows us to dynamically load
// libbinder_ndk at runtime, and make it possible to compile the code without
// the library present at compile time.

// TODO(mingcl): Consider if we want to check API level and include NDK headers
// normally if the level is high enough

namespace grpc_binder {
namespace ndk_util {

struct AIBinder;
struct AParcel;
struct AIBinder_Class;

// Only enum values used by the project is defined here
enum {
  FLAG_ONEWAY = 0x01,
};
enum {
  STATUS_OK = 0,
  STATUS_UNKNOWN_ERROR = (-2147483647 - 1),
};

typedef int32_t binder_status_t;
typedef uint32_t binder_flags_t;
typedef uint32_t transaction_code_t;

typedef bool (*AParcel_byteArrayAllocator)(void* arrayData, int32_t length,
                                           int8_t** outBuffer);
typedef bool (*AParcel_stringAllocator)(void* stringData, int32_t length,
                                        char** buffer);
typedef void* (*AIBinder_Class_onCreate)(void* args);
typedef void (*AIBinder_Class_onDestroy)(void* userData);
typedef binder_status_t (*AIBinder_Class_onTransact)(AIBinder* binder,
                                                     transaction_code_t code,
                                                     const AParcel* in,
                                                     AParcel* out);

void AIBinder_Class_disableInterfaceTokenHeader(AIBinder_Class* clazz);
void* AIBinder_getUserData(AIBinder* binder);
uid_t AIBinder_getCallingUid();
AIBinder* AIBinder_fromJavaBinder(JNIEnv* env, jobject binder);
AIBinder_Class* AIBinder_Class_define(const char* interfaceDescriptor,
                                      AIBinder_Class_onCreate onCreate,
                                      AIBinder_Class_onDestroy onDestroy,
                                      AIBinder_Class_onTransact onTransact);
AIBinder* AIBinder_new(const AIBinder_Class* clazz, void* args);
bool AIBinder_associateClass(AIBinder* binder, const AIBinder_Class* clazz);
void AIBinder_incStrong(AIBinder* binder);
void AIBinder_decStrong(AIBinder* binder);
binder_status_t AIBinder_transact(AIBinder* binder, transaction_code_t code,
                                  AParcel** in, AParcel** out,
                                  binder_flags_t flags);
binder_status_t AParcel_readByteArray(const AParcel* parcel, void* arrayData,
                                      AParcel_byteArrayAllocator allocator);
void AParcel_delete(AParcel* parcel);
int32_t AParcel_getDataSize(const AParcel* parcel);
binder_status_t AParcel_writeInt32(AParcel* parcel, int32_t value);
binder_status_t AParcel_writeInt64(AParcel* parcel, int64_t value);
binder_status_t AParcel_writeStrongBinder(AParcel* parcel, AIBinder* binder);
binder_status_t AParcel_writeString(AParcel* parcel, const char* string,
                                    int32_t length);
binder_status_t AParcel_readInt32(const AParcel* parcel, int32_t* value);
binder_status_t AParcel_readInt64(const AParcel* parcel, int64_t* value);
binder_status_t AParcel_readString(const AParcel* parcel, void* stringData,
                                   AParcel_stringAllocator allocator);
binder_status_t AParcel_readStrongBinder(const AParcel* parcel,
                                         AIBinder** binder);
binder_status_t AParcel_writeByteArray(AParcel* parcel, const int8_t* arrayData,
                                       int32_t length);
binder_status_t AIBinder_prepareTransaction(AIBinder* binder, AParcel** in);
jobject AIBinder_toJavaBinder(JNIEnv* env, AIBinder* binder);

}  // namespace ndk_util

}  // namespace grpc_binder

#endif /*GPR_SUPPORT_BINDER_TRANSPORT*/

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_NDK_BINDER_H
