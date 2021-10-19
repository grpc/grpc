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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CHANNEL_CREATE_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CHANNEL_CREATE_H

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/security_policy/security_policy.h"

#ifdef GPR_ANDROID

#include <jni.h>

#include "absl/strings/string_view.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpcpp/channel.h>
#include <grpcpp/support/channel_arguments.h>

namespace grpc {
namespace experimental {

// This need be called before calling CreateBinderChannel, and the thread need
// to be free before invoking CreateBinderChannel.
// TODO(mingcl): This method will be removed after we start creating client
// channel instead of direct channel
void BindToOnDeviceServerService(void* jni_env_void, jobject application,
                                 absl::string_view /*package_name*/,
                                 absl::string_view /*class_name*/);

// Need to be invoked after BindToOnDeviceServerService
// Create a new Channel from server package name and service class name
std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env_void, jobject application, absl::string_view package_name,
    absl::string_view class_name,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy);

// Need to be invoked after BindToOnDeviceServerService
// Create a new Channel from server package name and service class name and with
// custom channel arguments.
std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void* jni_env_void, jobject application, absl::string_view package_name,
    absl::string_view class_name,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    const ChannelArguments& args);

}  // namespace experimental
}  // namespace grpc

#endif

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_CLIENT_CHANNEL_CREATE_H
