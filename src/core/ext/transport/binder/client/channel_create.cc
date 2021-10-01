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

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/transport/binder/client/channel_create_impl.h"
#include "src/core/ext/transport/binder/client/connection_id_generator.h"
#include "src/core/ext/transport/binder/client/jni_utils.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/client/create_channel_internal.h"

namespace grpc {
namespace experimental {

void BindToOnDeviceServerService(void*, jobject, absl::string_view,
                                 absl::string_view) {}

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env, jobject application, absl::string_view package_name,
    absl::string_view class_name) {
  grpc::internal::GrpcLibrary init_lib;
  init_lib.init();
  return CreateCustomBinderChannel(jni_env, application, package_name,
                                   class_name, ChannelArguments());
}

std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void* jni_env, jobject application, absl::string_view package_name,
    absl::string_view class_name, const ChannelArguments& args) {
  grpc::internal::GrpcLibrary init_lib;
  init_lib.init();
  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);

  std::string connection_id = grpc_binder::GetConnectionIdGenerator()->Generate(
      package_name, class_name);
  string uri = "binder:" + connection_id;

  // TODO(mingcl): Consider if we want to delay the connection establishment
  // until SubchannelConnector is establishing connection, and if there is any
  // benifits doing that.
  // clang-format off
  grpc_binder::CallStaticJavaMethod(static_cast<JNIEnv*>(jni_env),
                       "io/grpc/binder/cpp/NativeConnectionHelper",
                       "tryEstablishConnection",
                       "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                       application, std::string(package_name), std::string(class_name), connection_id);
  // clang-format on

  grpc_channel_args* new_args;
  {
    grpc_arg arg = grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_ARG_SERVER_URI), const_cast<char*>(uri.c_str()));
    const char* to_remove[] = {GRPC_ARG_SERVER_URI};
    new_args = grpc_channel_args_copy_and_add_and_remove(&channel_args,
                                                         to_remove, 1, &arg, 1);
  }

  auto ret = CreateChannelInternal(
      /*host=*/std::string(package_name),
      ::grpc::internal::CreateClientBinderChannelImpl(new_args),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());

  grpc_channel_args_destroy(new_args);

  return ret;
}

}  // namespace experimental
}  // namespace grpc

#else  // !GPR_SUPPORT_BINDER_TRANSPORT

namespace grpc {
namespace experimental {

void BindToOnDeviceServerService(void* jni_env_void, jobject, absl::string_view,
                                 absl::string_view) {
  GPR_ASSERT(0);
}

std::shared_ptr<grpc::Channel> CreateBinderChannel(void*, jobject,
                                                   absl::string_view,
                                                   absl::string_view) {
  GPR_ASSERT(0);
  return {};
}

std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void*, jobject, absl::string_view, absl::string_view,
    const ChannelArguments&) {
  GPR_ASSERT(0);
  return {};
}

}  // namespace experimental
}  // namespace grpc

#endif  // GPR_SUPPORT_BINDER_TRANSPORT

#endif  // GPR_ANDROID
