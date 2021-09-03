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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_BINDER_ANDROID_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_BINDER_ANDROID_H

#include <grpc/impl/codegen/port_platform.h>

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_ibinder_jni.h>
#include <android/binder_interface_utils.h>
#include <jni.h>

#include <memory>

#include "absl/memory/memory.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"

namespace grpc_binder {

ndk::SpAIBinder FromJavaBinder(JNIEnv* jni_env, jobject binder);

class BinderAndroid;

class WritableParcelAndroid final : public WritableParcel {
 public:
  WritableParcelAndroid() = default;
  explicit WritableParcelAndroid(AParcel* parcel) : parcel_(parcel) {}
  ~WritableParcelAndroid() override = default;

  int32_t GetDataPosition() const override;
  absl::Status SetDataPosition(int32_t pos) override;
  absl::Status WriteInt32(int32_t data) override;
  absl::Status WriteBinder(HasRawBinder* binder) override;
  absl::Status WriteString(absl::string_view s) override;
  absl::Status WriteByteArray(const int8_t* buffer, int32_t length) override;

 private:
  AParcel* parcel_ = nullptr;

  friend class BinderAndroid;
};

class ReadableParcelAndroid final : public ReadableParcel {
 public:
  ReadableParcelAndroid() = default;
  // TODO(waynetu): Get rid of the const_cast.
  explicit ReadableParcelAndroid(const AParcel* parcel)
      : parcel_(const_cast<AParcel*>(parcel)) {}
  ~ReadableParcelAndroid() override = default;

  absl::Status ReadInt32(int32_t* data) const override;
  absl::Status ReadBinder(std::unique_ptr<Binder>* data) const override;
  absl::Status ReadByteArray(std::string* data) const override;
  // FIXME(waynetu): Fix the interface.
  absl::Status ReadString(char data[111]) const override;

 private:
  AParcel* parcel_ = nullptr;

  friend class BinderAndroid;
};

class BinderAndroid final : public Binder {
 public:
  explicit BinderAndroid(ndk::SpAIBinder binder)
      : binder_(binder),
        input_parcel_(absl::make_unique<WritableParcelAndroid>()),
        output_parcel_(absl::make_unique<ReadableParcelAndroid>()) {}
  ~BinderAndroid() override = default;

  void* GetRawBinder() override { return binder_.get(); }

  void Initialize() override;
  absl::Status PrepareTransaction() override;
  absl::Status Transact(BinderTransportTxCode tx_code) override;

  WritableParcel* GetWritableParcel() const override {
    return input_parcel_.get();
  }
  ReadableParcel* GetReadableParcel() const override {
    return output_parcel_.get();
  };

  std::unique_ptr<TransactionReceiver> ConstructTxReceiver(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb transact_cb) const override;

 private:
  ndk::SpAIBinder binder_;
  std::unique_ptr<WritableParcelAndroid> input_parcel_;
  std::unique_ptr<ReadableParcelAndroid> output_parcel_;
};

class TransactionReceiverAndroid final : public TransactionReceiver {
 public:
  TransactionReceiverAndroid(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      OnTransactCb transaction_cb);
  ~TransactionReceiverAndroid() override;
  void* GetRawBinder() override { return binder_; }

 private:
  AIBinder* binder_;
  OnTransactCb transact_cb_;
};

}  // namespace grpc_binder

#endif /*GPR_SUPPORT_BINDER_TRANSPORT*/

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_BINDER_ANDROID_H
