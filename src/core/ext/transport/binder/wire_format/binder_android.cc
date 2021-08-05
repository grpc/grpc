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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/wire_format/binder_android.h"

#if defined(ANDROID) || defined(__ANDROID__)

#include <grpc/support/log.h>

#include <map>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/gprpp/sync.h"

namespace {

struct AtomicCallback {
  explicit AtomicCallback(void* callback) : mu{}, callback(callback) {}
  grpc_core::Mutex mu;
  void* callback ABSL_GUARDED_BY(mu);
};

void* f_onCreate_with_mutex(void* callback) {
  return new AtomicCallback(callback);
}

void* f_onCreate_noop(void* args) { return nullptr; }
void f_onDestroy_noop(void* userData) {}

// TODO(mingcl): Consider if thread safety is a requirement here
binder_status_t f_onTransact(AIBinder* binder, transaction_code_t code,
                             const AParcel* in, AParcel* out) {
  gpr_log(GPR_INFO, __func__);
  gpr_log(GPR_INFO, "tx code = %u", code);
  auto* user_data =
      reinterpret_cast<AtomicCallback*>(AIBinder_getUserData(binder));
  grpc_core::MutexLock lock(&user_data->mu);

  // TODO(waynetu): What should be returned here?
  if (!user_data->callback) return STATUS_OK;

  auto* callback =
      reinterpret_cast<grpc_binder::TransactionReceiver::OnTransactCb*>(
          user_data->callback);
  // Wrap the parcel in a ReadableParcel.
  std::unique_ptr<grpc_binder::ReadableParcel> output =
      absl::make_unique<grpc_binder::ReadableParcelAndroid>(in);
  // The lock should be released "after" the callback finishes.
  absl::Status status = (*callback)(code, output.get());
  return status.ok() ? STATUS_OK : STATUS_UNKNOWN_ERROR;
}
}  // namespace

namespace grpc_binder {

ndk::SpAIBinder FromJavaBinder(JNIEnv* jni_env, jobject binder) {
  return ndk::SpAIBinder(AIBinder_fromJavaBinder(jni_env, binder));
}

TransactionReceiverAndroid::TransactionReceiverAndroid(OnTransactCb transact_cb)
    : transact_cb_(transact_cb) {
  // TODO(mingcl): For now interface descriptor is always empty, figure out if
  // we want it to be something more meaningful (we can probably manually change
  // interface descriptor by modifying Java code's reply to
  // os.IBinder.INTERFACE_TRANSACTION)
  AIBinder_Class* aibinder_class = AIBinder_Class_define(
      /*interfaceDescriptor=*/"", f_onCreate_with_mutex, f_onDestroy_noop,
      f_onTransact);

  // Pass the on-transact callback to the on-create function of the binder. The
  // on-create function equips the callback with a mutex and gives it to the
  // user data stored in the binder which can be retrieved later.
  binder_ = AIBinder_new(aibinder_class, &transact_cb_);
  GPR_ASSERT(binder_);
  gpr_log(GPR_INFO, "AIBinder_associateClass = %d",
          static_cast<int>(AIBinder_associateClass(binder_, aibinder_class)));
}

TransactionReceiverAndroid::~TransactionReceiverAndroid() {
  auto* user_data =
      reinterpret_cast<AtomicCallback*>(AIBinder_getUserData(binder_));
  {
    grpc_core::MutexLock lock(&user_data->mu);
    // Set the callback to null so that future calls to on-trasact are awared
    // that the transaction receiver had been deallocated.
    user_data->callback = nullptr;
  }
  // Release the binder.
  AIBinder_decStrong(binder_);
}

namespace {

binder_status_t f_onTransact_noop(AIBinder* binder, transaction_code_t code,
                                  const AParcel* in, AParcel* out) {
  return {};
}

void AssociateWithNoopClass(AIBinder* binder) {
  // Need to associate class before using it
  AIBinder_Class* aibinder_class = AIBinder_Class_define(
      "", f_onCreate_noop, f_onDestroy_noop, f_onTransact_noop);
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
  return AIBinder_transact(binder, static_cast<transaction_code_t>(tx_code),
                           &input_parcel_->parcel_, &output_parcel_->parcel_,
                           FLAG_ONEWAY) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AIBinder_transact failed");
}

std::unique_ptr<TransactionReceiver> BinderAndroid::ConstructTxReceiver(
    TransactionReceiver::OnTransactCb transact_cb) const {
  return absl::make_unique<TransactionReceiverAndroid>(transact_cb);
}

int32_t WritableParcelAndroid::GetDataPosition() const {
  return AParcel_getDataPosition(parcel_);
}

absl::Status WritableParcelAndroid::SetDataPosition(int32_t pos) {
  return AParcel_setDataPosition(parcel_, pos) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_setDataPosition failed");
}

absl::Status WritableParcelAndroid::WriteInt32(int32_t data) {
  return AParcel_writeInt32(parcel_, data) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_writeInt32 failed");
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

absl::Status ReadableParcelAndroid::ReadInt32(int32_t* data) const {
  return AParcel_readInt32(parcel_, data) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readInt32 failed");
}

absl::Status ReadableParcelAndroid::ReadBinder(
    std::unique_ptr<Binder>* data) const {
  AIBinder* binder;
  if (AParcel_readStrongBinder(parcel_, &binder) != STATUS_OK) {
    *data = nullptr;
    return absl::InternalError("AParcel_readStrongBinder failed");
  }
  *data = absl::make_unique<BinderAndroid>(ndk::SpAIBinder(binder));
  return absl::OkStatus();
}

namespace {

bool byte_array_allocator(void* arrayData, int32_t length, int8_t** outBuffer) {
  std::string tmp;
  tmp.resize(length);
  *reinterpret_cast<std::string*>(arrayData) = tmp;
  *outBuffer = reinterpret_cast<int8_t*>(
      reinterpret_cast<std::string*>(arrayData)->data());
  return true;
}

bool string_allocator(void* stringData, int32_t length, char** outBuffer) {
  if (length > 0) {
    // TODO(mingcl): Don't fix the length of the string
    GPR_ASSERT(length < 100);  // call should preallocate 100 bytes
    *outBuffer = reinterpret_cast<char*>(stringData);
  }
  return true;
}

}  // namespace

absl::Status ReadableParcelAndroid::ReadByteArray(std::string* data) const {
  return AParcel_readByteArray(parcel_, data, byte_array_allocator) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readByteArray failed");
}

absl::Status ReadableParcelAndroid::ReadString(char data[111]) const {
  return AParcel_readString(parcel_, data, string_allocator) == STATUS_OK
             ? absl::OkStatus()
             : absl::InternalError("AParcel_readString failed");
}

}  // namespace grpc_binder

#endif  // defined(ANDROID) || defined(__ANDROID__)
