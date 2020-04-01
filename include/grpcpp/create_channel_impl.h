/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCPP_CREATE_CHANNEL_IMPL_H
#define GRPCPP_CREATE_CHANNEL_IMPL_H

#include <memory>

#include <grpcpp/channel.h>
#include <grpcpp/impl/codegen/client_interceptor.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/config.h>

namespace grpc_impl {

/// Prevents the user from linking with an incompatible gRPC library.
///
/// The definition of some gRPC types change depending on some compiler options
/// (ASAN, TSAN). This can easily cause unexpected runtime issues if the client
/// code does not use the same compiler flags than the library. This function
/// helps detect and prevent such errors by causing linker errors instead.
static inline int PreventOneDefinitionRuleViolation() {
  int result = 0;

#ifdef GRPC_ASAN_ENABLED
  extern int grpc_must_be_compiled_with_asan;
  result += grpc_must_be_compiled_with_asan;
#else
  extern int grpc_must_be_compiled_without_asan;
  result += grpc_must_be_compiled_without_asan;
#endif

#ifdef GRPC_TSAN_ENABLED
  extern int grpc_must_be_compiled_with_tsan;
  result += grpc_must_be_compiled_with_tsan;
#else
  extern int grpc_must_be_compiled_without_tsan;
  result += grpc_must_be_compiled_without_tsan;
#endif

  return result;
}

/// Create a new \a Channel pointing to \a target.
///
/// \param target The URI of the endpoint to connect to.
/// \param creds Credentials to use for the created channel. If it does not
/// hold an object or is invalid, a lame channel (one on which all operations
/// fail) is returned.
std::shared_ptr<::grpc::Channel> CreateChannelImpl(
    const grpc::string& target,
    const std::shared_ptr<::grpc::ChannelCredentials>& creds);

/// Create a new \em custom \a Channel pointing to \a target.
///
/// \warning For advanced use and testing ONLY. Override default channel
/// arguments only if necessary.
///
/// \param target The URI of the endpoint to connect to.
/// \param creds Credentials to use for the created channel. If it does not
/// hold an object or is invalid, a lame channel (one on which all operations
/// fail) is returned.
/// \param args Options for channel creation.
std::shared_ptr<::grpc::Channel> CreateCustomChannelImpl(
    const grpc::string& target,
    const std::shared_ptr<::grpc::ChannelCredentials>& creds,
    const ::grpc::ChannelArguments& args);

namespace experimental {
/// Create a new \em custom \a Channel pointing to \a target with \a
/// interceptors being invoked per call.
///
/// \warning For advanced use and testing ONLY. Override default channel
/// arguments only if necessary.
///
/// \param target The URI of the endpoint to connect to.
/// \param creds Credentials to use for the created channel. If it does not
/// hold an object or is invalid, a lame channel (one on which all operations
/// fail) is returned.
/// \param args Options for channel creation.
std::shared_ptr<::grpc::Channel> CreateCustomChannelWithInterceptors(
    const grpc::string& target,
    const std::shared_ptr<grpc::ChannelCredentials>& creds,
    const ::grpc::ChannelArguments& args,
    std::vector<
        std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
        interceptor_creators);
}  // namespace experimental
}  // namespace grpc_impl

#endif  // GRPCPP_CREATE_CHANNEL_IMPL_H
