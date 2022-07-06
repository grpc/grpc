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

#ifndef GRPCPP_CREATE_CHANNEL_BINDER_H
#define GRPCPP_CREATE_CHANNEL_BINDER_H

#include <grpc/support/port_platform.h>

#ifdef GPR_ANDROID

#include <jni.h>

#include <memory>

#include "absl/strings/string_view.h"

#include <grpcpp/channel.h>
#include <grpcpp/security/binder_security_policy.h>
#include <grpcpp/support/channel_arguments.h>

namespace grpc {
namespace experimental {

/// EXPERIMENTAL Create a new \a Channel based on binder transport. The package
/// name and class name will be used identify the specific application component
/// to connect to.
///
/// \param jni_env Pointer to a JNIEnv structure
/// \param context The context that we will use to invoke \a bindService See
/// https://developer.android.com/reference/android/content/Context#bindService(android.content.Intent,%20android.content.ServiceConnection,%20int)
/// for detail.
/// \param package_name Package name of the component to be connected to
/// \param class_name Class name of the component to be connected to
/// \param security_policy Used for checking if remote component is allowed to
/// connect
std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env, jobject context, absl::string_view package_name,
    absl::string_view class_name,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy);

/// EXPERIMENTAL Create a new \a Channel based on binder transport. The package
/// name and class name will be used identify the specific application component
/// to connect to.
///
/// \param jni_env Pointer to a JNIEnv structure
/// \param context The context that we will use to invoke \a bindService See
/// https://developer.android.com/reference/android/content/Context#bindService(android.content.Intent,%20android.content.ServiceConnection,%20int)
/// for detail.
/// \param package_name Package name of the component to be connected to
/// \param class_name Class name of the component to be connected to
/// \param security_policy Used for checking if remote component is allowed to
/// connect
/// \param args Options for channel creation.
std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void* jni_env_void, jobject application, absl::string_view package_name,
    absl::string_view class_name,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    const ChannelArguments& args);

/// EXPERIMENTAL Create a new \a Channel based on binder transport.
///
/// \param jni_env Pointer to a JNIEnv structure
/// \param context The context that we will use to invoke \a bindService See
/// https://developer.android.com/reference/android/content/Context#bindService(android.content.Intent,%20android.content.ServiceConnection,%20int)
/// for detail.
/// \param uri An URI that can be parsed as an `Intent` with
/// https://developer.android.com/reference/android/content/Intent#parseUri(java.lang.String,%20int)
/// \param security_policy Used for checking if remote component is allowed to
/// connect
std::shared_ptr<grpc::Channel> CreateBinderChannel(
    void* jni_env, jobject context, absl::string_view uri,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy);

/// EXPERIMENTAL Create a new \a Channel based on binder transport.
///
/// \param jni_env Pointer to a JNIEnv structure
/// \param context The context that we will use to invoke \a bindService See
/// https://developer.android.com/reference/android/content/Context#bindService(android.content.Intent,%20android.content.ServiceConnection,%20int)
/// for detail.
/// \param uri An URI that can be parsed as an `Intent` with
/// https://developer.android.com/reference/android/content/Intent#parseUri(java.lang.String,%20int)
/// \param security_policy Used for checking if remote component is allowed to
/// connect
/// \param args Options for channel creation.
std::shared_ptr<grpc::Channel> CreateCustomBinderChannel(
    void* jni_env, jobject context, absl::string_view uri,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    const ChannelArguments& args);

/// EXPERIMENTAL Finds internal binder transport Java code. To create channels
/// in threads created in native code, it is required to call this function
/// once beforehand in a thread that is not created in native code.
/// See
/// https://developer.android.com/training/articles/perf-jni#faq:-why-didnt-findclass-find-my-class
/// for details of this limitation.
/// Returns true when the initialization is successful.
bool InitializeBinderChannelJavaClass(void* jni_env_void);

/// EXPERIMENTAL Alternative version of `InitializeBinderChannelJavaClass(void*
/// jni_env_void)`. This version used a user-specified function to find the
/// required internal Java class. When a class is found, the `class_finder`
/// function should return a local reference to the class (jclass type). The
/// returned jclass will then be used to create global reference for gRPC to use
/// it later. After that, gRPC will DeleteLocalRef the returned local reference.
bool InitializeBinderChannelJavaClass(
    void* jni_env_void, std::function<void*(std::string)> class_finder);

}  // namespace experimental
}  // namespace grpc

#endif

#endif  // GRPCPP_CREATE_CHANNEL_BINDER_H
