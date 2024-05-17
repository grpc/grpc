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

#include "src/core/ext/transport/binder/client/endpoint_binder_pool.h"

#include "absl/log/check.h"
#include "absl/log/log.h"

#include <grpc/support/port_platform.h>

#ifndef GRPC_NO_BINDER

#include "src/core/ext/transport/binder/client/jni_utils.h"

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <jni.h>

#include "src/core/ext/transport/binder/wire_format/binder_android.h"

extern "C" {
// Adds endpoint binder to binder pool when Java notify us that the endpoint
// binder is ready. This is called from GrpcBinderConnection.java
JNIEXPORT void JNICALL
Java_io_grpc_binder_cpp_GrpcBinderConnection_notifyConnected__Ljava_lang_String_2Landroid_os_IBinder_2(
    JNIEnv* jni_env, jobject, jstring conn_id_jstring, jobject ibinder) {
  jboolean isCopy;
  const char* conn_id = jni_env->GetStringUTFChars(conn_id_jstring, &isCopy);
  LOG(INFO) << __func__ << " invoked with conn_id = " << conn_id;
  CHECK_NE(ibinder, nullptr);
  grpc_binder::ndk_util::SpAIBinder aibinder =
      grpc_binder::FromJavaBinder(jni_env, ibinder);
  LOG(INFO) << __func__ << " got aibinder = " << aibinder.get();
  auto b = std::make_unique<grpc_binder::BinderAndroid>(aibinder);
  CHECK(b != nullptr);
  grpc_binder::GetEndpointBinderPool()->AddEndpointBinder(conn_id,
                                                          std::move(b));
  if (isCopy == JNI_TRUE) {
    jni_env->ReleaseStringUTFChars(conn_id_jstring, conn_id);
  }
}
}

#endif  // GPR_SUPPORT_BINDER_TRANSPORT

namespace grpc_binder {

void EndpointBinderPool::GetEndpointBinder(
    std::string conn_id,
    std::function<void(std::unique_ptr<grpc_binder::Binder>)> cb) {
  LOG(INFO) << "EndpointBinder requested. conn_id = " << conn_id;
  std::unique_ptr<grpc_binder::Binder> b;
  {
    grpc_core::MutexLock l(&m_);
    if (binder_map_.count(conn_id)) {
      b = std::move(binder_map_[conn_id]);
      binder_map_.erase(conn_id);
      CHECK(b != nullptr);
    } else {
      if (pending_requests_.count(conn_id) != 0) {
        LOG(ERROR) << "Duplicate GetEndpointBinder requested. conn_id = "
                   << conn_id;
        return;
      }
      pending_requests_[conn_id] = std::move(cb);
      return;
    }
  }
  CHECK(b != nullptr);
  cb(std::move(b));
}

void EndpointBinderPool::AddEndpointBinder(
    std::string conn_id, std::unique_ptr<grpc_binder::Binder> b) {
  LOG(INFO) << "EndpointBinder added. conn_id = " << conn_id;
  CHECK(b != nullptr);
  // cb will be set in the following block if there is a pending callback
  std::function<void(std::unique_ptr<grpc_binder::Binder>)> cb = nullptr;
  {
    grpc_core::MutexLock l(&m_);
    if (binder_map_.count(conn_id) != 0) {
      LOG(ERROR) << "EndpointBinder already in the pool. conn_id = " << conn_id;
      return;
    }
    if (pending_requests_.count(conn_id)) {
      cb = std::move(pending_requests_[conn_id]);
      pending_requests_.erase(conn_id);
    } else {
      binder_map_[conn_id] = std::move(b);
      b = nullptr;
    }
  }
  if (cb != nullptr) {
    cb(std::move(b));
  }
}

EndpointBinderPool* GetEndpointBinderPool() {
  static EndpointBinderPool* p = new EndpointBinderPool();
  return p;
}
}  // namespace grpc_binder
#endif
