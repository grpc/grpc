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
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/sync.h"

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
ndk_util::binder_status_t f_onTransact(ndk_util::AIBinder* binder,
                                       transaction_code_t code,
                                       const ndk_util::AParcel* in,
                                       ndk_util::AParcel* /*out*/) {
  gpr_log(GPR_INFO, __func__);
  gpr_log(GPR_INFO, "tx code = %u", code);

  auto* user_data =
      static_cast<BinderUserData*>(ndk_util::AIBinder_getUserData(binder));
  TransactionReceiver::OnTransactCb* callback = user_data->callback;
  // Wrap the parcel in a ReadableParcel.
  std::unique_ptr<ReadableParcel> output =
      std::make_unique<ReadableParcelAndroid>(in);
  // The lock should be released "after" the callback finishes.
  absl::Status status =
      (*callback)(code, output.get(), ndk_util::AIBinder_getCallingUid());
  if (status.ok()) {
    return ndk_util::STATUS_OK;
  } else {
    gpr_log(GPR_ERROR, "Callback failed: %s", status.ToString().c_str());
    return ndk_util::STATUS_UNKNOWN_ERROR;
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

ndk_util::binder_status_t AParcelReadString(const ndk_util::AParcel* parcel,
                                            std::string* str) {
  void* stringData = static_cast<void*>(str);
  return ndk_util::AParcel_readString(parcel, stringData, StdStringAllocator);
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

ndk_util::binder_status_t AParcelReadVector(const ndk_util::AParcel* parcel,
                                            std::vector<uint8_t>* vec) {
  void* vectorData = static_cast<void*>(vec);
  return ndk_util::AParcel_readByteArray(parcel, vectorData,
                                         StdVectorAllocator<int8_t>);
}

}  // namespace

ndk_util::SpAIBinder FromJavaBinder(JNIEnv* jni_env, jobject binder) {
  return ndk_util::SpAIBinder(
      ndk_util::AIBinder_fromJavaBinder(jni_env, binder));
}

TransactionReceiverAndroid::TransactionReceiverAndroid(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    OnTransactCb transact_cb)
    : transact_cb_(transact_cb) {
  // TODO(mingcl): For now interface descriptor is always empty, figure out if
  // we want it to be something more meaningful (we can probably manually change
  // interface descriptor by modifying Java code's reply to
  // os.IBinder.INTERFACE_TRANSACTION)
  ndk_util::AIBinder_Class* aibinder_class = ndk_util::AIBinder_Class_define(
      /*interfaceDescriptor=*/"", f_onCreate_userdata, f_onDestroy_delete,
      f_onTransact);

  ndk_util::AIBinder_Class_disableInterfaceTokenHeader(aibinder_class);

  // Pass the on-transact callback to the on-create function of the binder. The
  // on-create function equips the callback with a mutex and gives it to the
  // user data stored in the binder which can be retrieved later.
  // Also Ref() (called implicitly by the copy constructor of RefCountedPtr) the
  // wire reader so that it would not be destructed during the callback
  // invocation.
  OnCreateArgs args;
  args.wire_reader_ref = wire_reader_ref;
  args.callback = &transact_cb_;
  binder_ = ndk_util::AIBinder_new(aibinder_class, &args);
  GPR_ASSERT(binder_);
  gpr_log(GPR_INFO, "ndk_util::AIBinder_associateClass = %d",
          static_cast<int>(
              ndk_util::AIBinder_associateClass(binder_, aibinder_class)));
}

TransactionReceiverAndroid::~TransactionReceiverAndroid() {
  // Release the binder.
  ndk_util::AIBinder_decStrong(binder_);
}

namespace {

ndk_util::binder_status_t f_onTransact_noop(ndk_util::AIBinder* /*binder*/,
                                            transaction_code_t /*code*/,
                                            const ndk_util::AParcel* /*in*/,
                                            ndk_util::AParcel* /*out*/) {
  return {};
}

void AssociateWithNoopClass(ndk_util::AIBinder* binder) {
  // Need to associate class before using it
  ndk_util::AIBinder_Class* aibinder_class = ndk_util::AIBinder_Class_define(
      "", f_onCreate_noop, f_onDestroy_noop, f_onTransact_noop);

  ndk_util::AIBinder_Class_disableInterfaceTokenHeader(aibinder_class);

  gpr_log(GPR_INFO, "ndk_util::AIBinder_associateClass = %d",
          static_cast<int>(
              ndk_util::AIBinder_associateClass(binder, aibinder_class)));
}

}  // namespace

void BinderAndroid::Initialize() {
  ndk_util::AIBinder* binder = binder_.get();
  AssociateWithNoopClass(binder);
}

absl::Status BinderAndroid::PrepareTransaction() {
  ndk_util::AIBinder* binder = binder_.get();
  return ndk_util::AIBinder_prepareTransaction(
             binder, &input_parcel_->parcel_) == ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError(
                   "ndk_util::AIBinder_prepareTransaction failed");
}

absl::Status BinderAndroid::Transact(BinderTransportTxCode tx_code) {
  ndk_util::AIBinder* binder = binder_.get();
  // We only do one-way transaction and thus the output parcel is never used.
  ndk_util::AParcel* unused_output_parcel;
  absl::Status result =
      (ndk_util::AIBinder_transact(
           binder, static_cast<transaction_code_t>(tx_code),
           &input_parcel_->parcel_, &unused_output_parcel,
           ndk_util::FLAG_ONEWAY) == ndk_util::STATUS_OK)
          ? absl::OkStatus()
          : absl::InternalError("ndk_util::AIBinder_transact failed");
  ndk_util::AParcel_delete(unused_output_parcel);
  return result;
}

std::unique_ptr<TransactionReceiver> BinderAndroid::ConstructTxReceiver(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb transact_cb) const {
  return std::make_unique<TransactionReceiverAndroid>(wire_reader_ref,
                                                      transact_cb);
}

int32_t WritableParcelAndroid::GetDataSize() const {
  return ndk_util::AParcel_getDataSize(parcel_);
}

absl::Status WritableParcelAndroid::WriteInt32(int32_t data) {
  return ndk_util::AParcel_writeInt32(parcel_, data) == ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeInt32 failed");
}

absl::Status WritableParcelAndroid::WriteInt64(int64_t data) {
  return ndk_util::AParcel_writeInt64(parcel_, data) == ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeInt64 failed");
}

absl::Status WritableParcelAndroid::WriteBinder(HasRawBinder* binder) {
  return ndk_util::AParcel_writeStrongBinder(
             parcel_, reinterpret_cast<ndk_util::AIBinder*>(
                          binder->GetRawBinder())) == ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeStrongBinder failed");
}

absl::Status WritableParcelAndroid::WriteString(absl::string_view s) {
  return ndk_util::AParcel_writeString(parcel_, s.data(), s.length()) ==
                 ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeString failed");
}

absl::Status WritableParcelAndroid::WriteByteArray(const int8_t* buffer,
                                                   int32_t length) {
  return ndk_util::AParcel_writeByteArray(parcel_, buffer, length) ==
                 ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeByteArray failed");
}

int32_t ReadableParcelAndroid::GetDataSize() const {
  return ndk_util::AParcel_getDataSize(parcel_);
}

absl::Status ReadableParcelAndroid::ReadInt32(int32_t* data) {
  return ndk_util::AParcel_readInt32(parcel_, data) == ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readInt32 failed");
}

absl::Status ReadableParcelAndroid::ReadInt64(int64_t* data) {
  return ndk_util::AParcel_readInt64(parcel_, data) == ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readInt64 failed");
}

absl::Status ReadableParcelAndroid::ReadBinder(std::unique_ptr<Binder>* data) {
  ndk_util::AIBinder* binder;
  if (AParcel_readStrongBinder(parcel_, &binder) != ndk_util::STATUS_OK) {
    *data = nullptr;
    return absl::InternalError("AParcel_readStrongBinder failed");
  }
  *data = std::make_unique<BinderAndroid>(ndk_util::SpAIBinder(binder));
  return absl::OkStatus();
}

absl::Status ReadableParcelAndroid::ReadByteArray(std::string* data) {
  std::vector<uint8_t> vec;
  if (AParcelReadVector(parcel_, &vec) == ndk_util::STATUS_OK) {
    data->resize(vec.size());
    if (!vec.empty()) {
      memcpy(&((*data)[0]), vec.data(), vec.size());
    }
    return absl::OkStatus();
  }
  return absl::InternalError("AParcel_readByteArray failed");
}

absl::Status ReadableParcelAndroid::ReadString(std::string* str) {
  return AParcelReadString(parcel_, str) == ndk_util::STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readString failed");
}

}  // namespace grpc_binder

#endif  // GPR_SUPPORT_BINDER_TRANSPORT
#endif
