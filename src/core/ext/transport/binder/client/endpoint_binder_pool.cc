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

#include "src/core/ext/transport/binder/client/endpoint_binder_pool.h"

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
  gpr_log(GPR_INFO, "%s invoked with conn_id = %s", __func__, conn_id);
  GPR_ASSERT(ibinder != nullptr);
  grpc_binder::ndk_util::SpAIBinder aibinder =
      grpc_binder::FromJavaBinder(jni_env, ibinder);
  gpr_log(GPR_INFO, "%s got aibinder = %p", __func__, aibinder.get());
  auto b = absl::make_unique<grpc_binder::BinderAndroid>(aibinder);
  GPR_ASSERT(b != nullptr);
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
  gpr_log(GPR_INFO, "EndpointBinder requested. conn_id = %s", conn_id.c_str());
  std::unique_ptr<grpc_binder::Binder> b;
  {
    grpc_core::MutexLock l(&m_);
    if (binder_map_.count(conn_id)) {
      b = std::move(binder_map_[conn_id]);
      binder_map_.erase(conn_id);
      GPR_ASSERT(b != nullptr);
    } else {
      if (pending_requests_.count(conn_id) != 0) {
        gpr_log(GPR_ERROR,
                "Duplicate GetEndpointBinder requested. conn_id = %s",
                conn_id.c_str());
        return;
      }
      pending_requests_[conn_id] = std::move(cb);
      return;
    }
  }
  GPR_ASSERT(b != nullptr);
  cb(std::move(b));
}

void EndpointBinderPool::AddEndpointBinder(
    std::string conn_id, std::unique_ptr<grpc_binder::Binder> b) {
  gpr_log(GPR_INFO, "EndpointBinder added. conn_id = %s", conn_id.c_str());
  GPR_ASSERT(b != nullptr);
  // cb will be set in the following block if there is a pending callback
  std::function<void(std::unique_ptr<grpc_binder::Binder>)> cb = nullptr;
  {
    grpc_core::MutexLock l(&m_);
    if (binder_map_.count(conn_id) != 0) {
      gpr_log(GPR_ERROR, "EndpointBinder already in the pool. conn_id = %s",
              conn_id.c_str());
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
