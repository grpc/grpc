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

#include "src/core/ext/transport/binder/client/channel_create.h"

// The interface is only defined if GPR_ANDROID is defined, because some
// arguments requires JNI.
// Furthermore, the interface is non-phony only when
// GPR_SUPPORT_BINDER_TRANSPORT is true because actual implementation of binder
// transport requires newer version of NDK API

#ifdef GPR_ANDROID

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <grpc/support/port_platform.h>

#include <android/binder_auto_utils.h>
#include <android/binder_ibinder.h>
#include <android/binder_ibinder_jni.h>
#include <android/binder_interface_utils.h>

#include "absl/memory/memory.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/support/log.h>
#include <grpcpp/impl/grpc_library.h>

#include "src/core/ext/transport/binder/client/channel_create_impl.h"
#include "src/core/ext/transport/binder/client/endpoint_binder_pool.h"
#include "src/core/ext/transport/binder/client/jni_utils.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/client/create_channel_internal.h"

namespace grpc {
namespace experimental {

// TODO(mingcl): To support multiple binder transport connection at the same
// time, we will need to generate unique connection id for each connection.
// For now we use a fixed connection id. This will be fixed in the next PR
std::string kConnectionId = "connection_id_placeholder";

void BindToOnDeviceServerService(void* jni_env_void, jobject application,
                                 absl::string_view package_name,
                                 absl::string_view class_name) {
  // Init gRPC library first so gpr_log works
  grpc::internal::GrpcLibrary init_lib;
  init_lib.init();

  JNIEnv* jni_env = static_cast<JNIEnv*>(jni_env_void);

  // clang-format off
  grpc_binder::CallStaticJavaMethod(jni_env,
                       "io/grpc/binder/cpp/NativeConnectionHelper",
                       "tryEstablishConnection",
                       "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                       application, std::string(package_name), std::string(class_name), kConnectionId);
  // clang-format on
}

// BindToOndeviceServerService need to be called before this, in a different
// task (due to Android API design). (Reference:
// https://stackoverflow.com/a/3055749)
std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env_void, jobject application, absl::string_view package_name,
    absl::string_view class_name,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  return CreateCustomBinderChannel(jni_env_void, application, package_name,
                                   class_name, security_policy,
                                   ChannelArguments());
}

// BindToOndeviceServerService need to be called before this, in a different
// task (due to Android API design). (Reference:
// https://stackoverflow.com/a/3055749)
std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void*, jobject /*application*/, absl::string_view /*package_name*/,
    absl::string_view /*class_name*/,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    const ChannelArguments& args) {
  GPR_ASSERT(security_policy != nullptr);

  std::unique_ptr<grpc_binder::Binder> endpoint_binder;
  grpc_binder::GetEndpointBinderPool()->GetEndpointBinder(
      kConnectionId, [&](std::unique_ptr<grpc_binder::Binder> e) {
        endpoint_binder = std::move(e);
      });
  // This assumes the above callback will be called immediately before
  // `GetEndpointBinder` returns
  GPR_ASSERT(endpoint_binder != nullptr);

  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);
  return CreateChannelInternal(
      "",
      ::grpc::internal::CreateChannelFromBinderImpl(
          std::move(endpoint_binder), security_policy, &channel_args),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());
}

}  // namespace experimental
}  // namespace grpc

#else  // !GPR_SUPPORT_BINDER_TRANSPORT

namespace grpc {
namespace experimental {

void BindToOnDeviceServerService(void*, jobject, absl::string_view,
                                 absl::string_view) {
  GPR_ASSERT(0);
}

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void*, jobject, absl::string_view, absl::string_view,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>) {
  GPR_ASSERT(0);
  return {};
}

std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void*, jobject, absl::string_view, absl::string_view,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>,
    const ChannelArguments&) {
  GPR_ASSERT(0);
  return {};
}

}  // namespace experimental
}  // namespace grpc

#endif  // GPR_SUPPORT_BINDER_TRANSPORT

#endif  // GPR_ANDROID
