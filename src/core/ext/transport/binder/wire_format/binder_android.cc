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

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <map>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/gprpp/sync.h"

extern "C" {
// TODO(mingcl): This function is introduced at API level 32 and is not
// available in any NDK release yet. So we export it weakly so that we can use
// it without triggering undefined reference error. Its purpose is to disable
// header in Parcel to conform to the BinderChannel wire format.
extern void AIBinder_Class_disableInterfaceTokenHeader(AIBinder_Class* clazz)
    __attribute__((weak));
// This is released in API level 31.
extern int32_t AParcel_getDataSize(const AParcel* parcel) __attribute__((weak));
}

namespace grpc_binder {
namespace {

struct BinderUserData {
  explicit BinderUserData(grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
                          TransactionReceiver::OnTransactCb* callback)
      : wire_reader_ref(wire_reader_ref), callback(callback) {}
  grpc_core::RefCountedPtr<WireReader> wire_reader_ref;
  TransactionReceiver::OnTransactCb* callback;
};

struct OnCreateArgs {
  grpc_core::RefCountedPtr<WireReader> wire_reader_ref;
  TransactionReceiver::OnTransactCb* callback;
};

void* f_onCreate_userdata(void* data) {
  auto* args = static_cast<OnCreateArgs*>(data);
  return new BinderUserData(args->wire_reader_ref, args->callback);
}

void f_onDestroy_delete(void* data) {
  auto* user_data = static_cast<BinderUserData*>(data);
  delete user_data;
}

void* f_onCreate_noop(void* /*args*/) { return nullptr; }
void f_onDestroy_noop(void* /*userData*/) {}

// TODO(mingcl): Consider if thread safety is a requirement here
binder_status_t f_onTransact(AIBinder* binder, transaction_code_t code,
                             const AParcel* in, AParcel* /*out*/) {
  gpr_log(GPR_INFO, __func__);
  gpr_log(GPR_INFO, "tx code = %u", code);

  auto* user_data = static_cast<BinderUserData*>(AIBinder_getUserData(binder));
  TransactionReceiver::OnTransactCb* callback = user_data->callback;
  // Wrap the parcel in a ReadableParcel.
  std::unique_ptr<ReadableParcel> output =
      absl::make_unique<ReadableParcelAndroid>(in);
  // The lock should be released "after" the callback finishes.
  absl::Status status =
      (*callback)(code, output.get(), AIBinder_getCallingUid());
  if (status.ok()) {
    return STATUS_OK;
  } else {
    gpr_log(GPR_ERROR, "Callback failed: %s", status.ToString().c_str());
    return STATUS_UNKNOWN_ERROR;
  }
}

// StdStringAllocator, ReadString, StdVectorAllocator, and ReadVector's
// implementations are copied from android/binder_parcel_utils.h
// We cannot include the header because it does not compile in C++11

bool StdStringAllocator(void* stringData, int32_t length, char** buffer) {
  if (length <= 0) return false;

  std::string* str = static_cast<std::string*>(stringData);
  str->resize(static_cast<size_t>(length) - 1);
  *buffer = &(*str)[0];
  return true;
}

binder_status_t AParcelReadString(const AParcel* parcel, std::string* str) {
  void* stringData = static_cast<void*>(str);
  return AParcel_readString(parcel, stringData, StdStringAllocator);
}

template <typename T>
bool StdVectorAllocator(void* vectorData, int32_t length, T** outBuffer) {
  if (length < 0) return false;

  std::vector<T>* vec = static_cast<std::vector<T>*>(vectorData);
  if (static_cast<size_t>(length) > vec->max_size()) return false;

  vec->resize(static_cast<size_t>(length));
  *outBuffer = vec->data();
  return true;
}

binder_status_t AParcelReadVector(const AParcel* parcel,
                                  std::vector<uint8_t>* vec) {
  void* vectorData = static_cast<void*>(vec);
  return AParcel_readByteArray(parcel, vectorData, StdVectorAllocator<int8_t>);
}

}  // namespace

ndk::SpAIBinder FromJavaBinder(JNIEnv* jni_env, jobject binder) {
  return ndk::SpAIBinder(AIBinder_fromJavaBinder(jni_env, binder));
}

TransactionReceiverAndroid::TransactionReceiverAndroid(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    OnTransactCb transact_cb)
    : transact_cb_(transact_cb) {
  // TODO(mingcl): For now interface descriptor is always empty, figure out if
  // we want it to be something more meaningful (we can probably manually change
  // interface descriptor by modifying Java code's reply to
  // os.IBinder.INTERFACE_TRANSACTION)
  AIBinder_Class* aibinder_class = AIBinder_Class_define(
      /*interfaceDescriptor=*/"", f_onCreate_userdata, f_onDestroy_delete,
      f_onTransact);

  if (AIBinder_Class_disableInterfaceTokenHeader) {
    AIBinder_Class_disableInterfaceTokenHeader(aibinder_class);
  } else {
    // TODO(mingcl): Make this a fatal error
    gpr_log(GPR_ERROR,
            "AIBinder_Class_disableInterfaceTokenHeader remain unresolved. "
            "This BinderTransport implementation contains header and is not "
            "compatible with Java's implementation");
  }

  // Pass the on-transact callback to the on-create function of the binder. The
  // on-create function equips the callback with a mutex and gives it to the
  // user data stored in the binder which can be retrieved later.
  // Also Ref() (called implicitly by the copy constructor of RefCountedPtr) the
  // wire reader so that it would not be destructed during the callback
  // invocation.
  OnCreateArgs args;
  args.wire_reader_ref = wire_reader_ref;
  args.callback = &transact_cb_;
  binder_ = AIBinder_new(aibinder_class, &args);
  GPR_ASSERT(binder_);
  gpr_log(GPR_INFO, "AIBinder_associateClass = %d",
          static_cast<int>(AIBinder_associateClass(binder_, aibinder_class)));
}

TransactionReceiverAndroid::~TransactionReceiverAndroid() {
  // Release the binder.
  AIBinder_decStrong(binder_);
}

namespace {

binder_status_t f_onTransact_noop(AIBinder* /*binder*/,
                                  transaction_code_t /*code*/,
                                  const AParcel* /*in*/, AParcel* /*out*/) {
  return {};
}

void AssociateWithNoopClass(AIBinder* binder) {
  // Need to associate class before using it
  AIBinder_Class* aibinder_class = AIBinder_Class_define(
      "", f_onCreate_noop, f_onDestroy_noop, f_onTransact_noop);

  if (AIBinder_Class_disableInterfaceTokenHeader) {
    AIBinder_Class_disableInterfaceTokenHeader(aibinder_class);
  } else {
    // TODO(mingcl): Make this a fatal error
    gpr_log(GPR_ERROR,
            "AIBinder_Class_disableInterfaceTokenHeader remain unresolved. "
            "This BinderTransport implementation contains header and is not "
            "compatible with Java's implementation");
  }

  gpr_log(GPR_INFO, "AIBinder_associateClass = %d",
          static_cast<int>(AIBinder_associateClass(binder, aibinder_class)));
}

}  // namespace

void BinderAndroid::Initialize() {
  AIBinder* binder = binder_.get();
  AssociateWithNoopClass(binder);
}

absl::Status BinderAndroid::PrepareTransaction() {
  AIBinder* binder = binder_.get();
  return AIBinder_prepareTransaction(binder, &input_parcel_->parcel_) ==
                 STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AIBinder_prepareTransaction failed");
}

absl::Status BinderAndroid::Transact(BinderTransportTxCode tx_code) {
  AIBinder* binder = binder_.get();
  // We only do one-way transaction and thus the output parcel is never used.
  AParcel* unused_output_parcel;
  absl::Status result =
      (AIBinder_transact(binder, static_cast<transaction_code_t>(tx_code),
                         &input_parcel_->parcel_, &unused_output_parcel,
                         FLAG_ONEWAY) == STATUS_OK)
          ? absl::OkStatus()
          : absl::InternalError("AIBinder_transact failed");
  AParcel_delete(unused_output_parcel);
  return result;
}

std::unique_ptr<TransactionReceiver> BinderAndroid::ConstructTxReceiver(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb transact_cb) const {
  return absl::make_unique<TransactionReceiverAndroid>(wire_reader_ref,
                                                       transact_cb);
}

int32_t WritableParcelAndroid::GetDataSize() const {
  if (AParcel_getDataSize) {
    return AParcel_getDataSize(parcel_);
  } else {
    gpr_log(GPR_INFO, "[Warning] AParcel_getDataSize is not available");
    return 0;
  }
}

absl::Status WritableParcelAndroid::WriteInt32(int32_t data) {
  return AParcel_writeInt32(parcel_, data) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeInt32 failed");
}

absl::Status WritableParcelAndroid::WriteInt64(int64_t data) {
  return AParcel_writeInt64(parcel_, data) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeInt64 failed");
}

absl::Status WritableParcelAndroid::WriteBinder(HasRawBinder* binder) {
  return AParcel_writeStrongBinder(
             parcel_, reinterpret_cast<AIBinder*>(binder->GetRawBinder())) ==
                 STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeStrongBinder failed");
}

absl::Status WritableParcelAndroid::WriteString(absl::string_view s) {
  return AParcel_writeString(parcel_, s.data(), s.length()) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeString failed");
}

absl::Status WritableParcelAndroid::WriteByteArray(const int8_t* buffer,
                                                   int32_t length) {
  return AParcel_writeByteArray(parcel_, buffer, length) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeByteArray failed");
}

int32_t ReadableParcelAndroid::GetDataSize() const {
  if (AParcel_getDataSize) {
    return AParcel_getDataSize(parcel_);
  } else {
    gpr_log(GPR_INFO, "[Warning] AParcel_getDataSize is not available");
    return 0;
  }
}

absl::Status ReadableParcelAndroid::ReadInt32(int32_t* data) {
  return AParcel_readInt32(parcel_, data) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readInt32 failed");
}

absl::Status ReadableParcelAndroid::ReadInt64(int64_t* data) {
  return AParcel_readInt64(parcel_, data) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readInt64 failed");
}

absl::Status ReadableParcelAndroid::ReadBinder(std::unique_ptr<Binder>* data) {
  AIBinder* binder;
  if (AParcel_readStrongBinder(parcel_, &binder) != STATUS_OK) {
    *data = nullptr;
    return absl::InternalError("AParcel_readStrongBinder failed");
  }
  *data = absl::make_unique<BinderAndroid>(ndk::SpAIBinder(binder));
  return absl::OkStatus();
}

absl::Status ReadableParcelAndroid::ReadByteArray(std::string* data) {
  std::vector<uint8_t> vec;
  if (AParcelReadVector(parcel_, &vec) == STATUS_OK) {
    data->resize(vec.size());
    if (!vec.empty()) {
      memcpy(&((*data)[0]), vec.data(), vec.size());
    }
    return absl::OkStatus();
  }
  return absl::InternalError("AParcel_readByteArray failed");
}

absl::Status ReadableParcelAndroid::ReadString(std::string* str) {
  return AParcelReadString(parcel_, str) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readString failed");
}

}  // namespace grpc_binder

#endif  // GPR_SUPPORT_BINDER_TRANSPORT
#endif
