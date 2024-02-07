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

#include <grpcpp/create_channel_binder.h>

// The interface is only defined if GPR_ANDROID is defined, because some
// arguments requires JNI.
// Furthermore, the interface is non-phony only when
// GPR_SUPPORT_BINDER_TRANSPORT is true because actual implementation of binder
// transport requires newer version of NDK API

#ifdef GPR_ANDROID

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"

#ifdef GPR_SUPPORT_BINDER_TRANSPORT

#include <grpc/support/port_platform.h>

#include "absl/memory/memory.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpcpp/impl/grpc_library.h>

#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/ext/transport/binder/client/channel_create_impl.h"
#include "src/core/ext/transport/binder/client/connection_id_generator.h"
#include "src/core/ext/transport/binder/client/endpoint_binder_pool.h"
#include "src/core/ext/transport/binder/client/jni_utils.h"
#include "src/core/ext/transport/binder/client/security_policy_setting.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/binder_android.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport.h"
#include "src/cpp/client/create_channel_internal.h"

namespace {
// grpc.io.action.BIND is the standard action name for binding to binder
// transport server.
const char* kStandardActionName = "grpc.io.action.BIND";
}  // namespace

namespace grpc {
namespace experimental {

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env_void, jobject context, absl::string_view package_name,
    absl::string_view class_name,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  return CreateCustomBinderChannel(jni_env_void, context, package_name,
                                   class_name, security_policy,
                                   ChannelArguments());
}

std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void* jni_env_void, jobject context, absl::string_view package_name,
    absl::string_view class_name,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    const ChannelArguments& args) {
  return CreateCustomBinderChannel(
      jni_env_void, context,
      absl::Substitute("android-app://$0#Intent;action=$1;component=$0/$2;end",
                       package_name, kStandardActionName, class_name),
      security_policy, args);
}

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env_void, jobject context, absl::string_view uri,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  return CreateCustomBinderChannel(jni_env_void, context, uri, security_policy,
                                   ChannelArguments());
}

std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void* jni_env_void, jobject context, absl::string_view uri,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    const ChannelArguments& args) {
  grpc_init();

  GPR_ASSERT(jni_env_void != nullptr);
  GPR_ASSERT(security_policy != nullptr);

  // Generate an unique connection ID that identifies this connection (Useful
  // for mapping connection between Java and C++ code).
  std::string connection_id =
      grpc_binder::GetConnectionIdGenerator()->Generate(uri);

  gpr_log(GPR_ERROR, "connection id is %s", connection_id.c_str());

  // After invoking this Java method, Java code will put endpoint binder into
  // `EndpointBinderPool` after the connection succeeds
  // TODO(mingcl): Consider if we want to delay the connection establishment
  // until SubchannelConnector start establishing connection. For now we don't
  // see any benifits doing that.
  grpc_binder::TryEstablishConnectionWithUri(static_cast<JNIEnv*>(jni_env_void),
                                             context, uri, connection_id);

  grpc_channel_args channel_args;
  args.SetChannelArgs(&channel_args);

  // Set server URI to a URI that contains connection id. The URI will be used
  // by subchannel connector to obtain correct endpoint binder from
  // `EndpointBinderPool`.
  grpc_channel_args* new_args;
  {
    string server_uri = "binder:" + connection_id;
    grpc_arg server_uri_arg =
        grpc_channel_arg_string_create(const_cast<char*>(GRPC_ARG_SERVER_URI),
                                       const_cast<char*>(server_uri.c_str()));
    const char* to_remove[] = {GRPC_ARG_SERVER_URI};
    new_args = grpc_channel_args_copy_and_add_and_remove(
        &channel_args, to_remove, 1, &server_uri_arg, 1);
  }

  grpc_binder::GetSecurityPolicySetting()->Set(connection_id, security_policy);

  auto channel = CreateChannelInternal(
      "", grpc::internal::CreateClientBinderChannelImpl(new_args),
      std::vector<
          std::unique_ptr<experimental::ClientInterceptorFactoryInterface>>());

  grpc_channel_args_destroy(new_args);

  return channel;
}

bool InitializeBinderChannelJavaClass(void* jni_env_void) {
  return grpc_binder::FindNativeConnectionHelper(
             static_cast<JNIEnv*>(jni_env_void)) != nullptr;
}

bool InitializeBinderChannelJavaClass(
    void* jni_env_void, std::function<void*(std::string)> class_finder) {
  return grpc_binder::FindNativeConnectionHelper(
             static_cast<JNIEnv*>(jni_env_void), class_finder) != nullptr;
}

}  // namespace experimental
}  // namespace grpc

#else  // !GPR_SUPPORT_BINDER_TRANSPORT

namespace grpc {
namespace experimental {

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void*, jobject, absl::string_view, absl::string_view,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>) {
  gpr_log(GPR_ERROR,
          "This APK is compiled with Android API level = %d, which is not "
          "supported. See port_platform.h for supported versions.",
          __ANDROID_API__);
  GPR_ASSERT(0);
  return {};
}

std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void*, jobject, absl::string_view, absl::string_view,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>,
    const ChannelArguments&) {
  gpr_log(GPR_ERROR,
          "This APK is compiled with Android API level = %d, which is not "
          "supported. See port_platform.h for supported versions.",
          __ANDROID_API__);
  GPR_ASSERT(0);
  return {};
}

std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void*, jobject, absl::string_view,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>) {
  gpr_log(GPR_ERROR,
          "This APK is compiled with Android API level = %d, which is not "
          "supported. See port_platform.h for supported versions.",
          __ANDROID_API__);
  GPR_ASSERT(0);
  return {};
}

std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void*, jobject, absl::string_view,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>,
    const ChannelArguments&) {
  gpr_log(GPR_ERROR,
          "This APK is compiled with Android API level = %d, which is not "
          "supported. See port_platform.h for supported versions.",
          __ANDROID_API__);
  GPR_ASSERT(0);
  return {};
}

bool InitializeBinderChannelJavaClass(void* jni_env_void) {
  gpr_log(GPR_ERROR,
          "This APK is compiled with Android API level = %d, which is not "
          "supported. See port_platform.h for supported versions.",
          __ANDROID_API__);
  GPR_ASSERT(0);
  return {};
}

bool InitializeBinderChannelJavaClass(
    void* jni_env_void, std::function<void*(std::string)> class_finder) {
  gpr_log(GPR_ERROR,
          "This APK is compiled with Android API level = %d, which is not "
          "supported. See port_platform.h for supported versions.",
          __ANDROID_API__);
  GPR_ASSERT(0);
  return {};
}

}  // namespace experimental
}  // namespace grpc

#endif  // GPR_SUPPORT_BINDER_TRANSPORT

#endif  // GPR_ANDROID

#endif  // GRPC_NO_BINDER
