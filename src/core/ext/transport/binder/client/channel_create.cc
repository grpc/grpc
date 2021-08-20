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

#include "src/core/ext/transport/binder/client/channel_create.h"

#if defined(ANDROID) || defined(__ANDROID__)

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_ibinder_jni.h>
#include <android/binder_interface_utils.h>
#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpcpp/impl/grpc_library.h>

#include "absl/memory/memory.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "src/core/ext/transport/binder/client/channel_create_impl.h"
#include "src/core/ext/transport/binder/client/jni_utils.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/client/create_channel_internal.h"

namespace grpc {
namespace experimental {

// This should be called before calling CreateBinderChannel
// TODO(mingcl): Pass package_name and class_name down to connection helper
// TODO(mingcl): Invoke a callback and pass binder object to caller after a
// successful bind
void BindToOnDeviceServerService(void* jni_env_void, jobject application,
                                 absl::string_view /*package_name*/,
                                 absl::string_view /*class_name*/
) {
  // Init gRPC library first so gpr_log works
  grpc::internal::GrpcLibrary init_lib;
  init_lib.init();

  JNIEnv* jni_env = static_cast<JNIEnv*>(jni_env_void);

  // clang-format off
  CallStaticJavaMethod(jni_env,
                       "io/grpc/binder/cpp/NativeConnectionHelper",
                       "tryEstablishConnection",
                       "(Landroid/content/Context;)V",
                       application);
  // clang-format on
}

// BindToOndeviceServerService need to be called before this, in a different
// task (due to Android API design). (Reference:
// https://stackoverflow.com/a/3055749)
// TODO(mingcl): Support multiple endpoint binder objects
std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env_void, jobject /*application*/,
    absl::string_view /*package_name*/, absl::string_view /*class_name*/) {
  JNIEnv* jni_env = static_cast<JNIEnv*>(jni_env_void);

  // clang-format off
  jobject object = CallStaticJavaMethodForObject(
      jni_env,
      "io/grpc/binder/cpp/NativeConnectionHelper",
      "getServiceBinder",
      "()Landroid/os/IBinder;");
  // clang-format on

  return CreateChannelInternal(
      "",
      ::grpc::internal::CreateChannelFromBinderImpl(
          absl::make_unique<grpc_binder::BinderAndroid>(
              grpc_binder::FromJavaBinder(jni_env, object)),
          nullptr),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
}

}  // namespace experimental
}  // namespace grpc

#endif  // ANDROID
