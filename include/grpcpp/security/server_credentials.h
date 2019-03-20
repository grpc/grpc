/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPCPP_SECURITY_SERVER_CREDENTIALS_H
#define GRPCPP_SECURITY_SERVER_CREDENTIALS_H

#include <grpcpp/security/server_credentials_impl.h>

namespace grpc {

typedef ::grpc_impl::ServerCredentials ServerCredentials;
typedef ::grpc_impl::SslServerCredentialsOptions SslServerCredentialsOptions;

static inline std::shared_ptr<ServerCredentials> SslServerCredentials(
    const SslServerCredentialsOptions& options) {
  return ::grpc_impl::SslServerCredentials(options);
}

static inline std::shared_ptr<ServerCredentials> InsecureServerCredentials() {
  return ::grpc_impl::InsecureServerCredentials();
}

namespace experimental {

typedef ::grpc_impl::experimental::AltsServerCredentialsOptions AltsServerCredentialsOptions;

static inline std::shared_ptr<ServerCredentials> AltsServerCredentials(
    const AltsServerCredentialsOptions& options) {
  return ::grpc_impl::experimental::AltsServerCredentials(options);
}

static inline std::shared_ptr<ServerCredentials> LocalServerCredentials(
    grpc_local_connect_type type) {
  return ::grpc_impl::experimental::LocalServerCredentials(type);
}

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_SERVER_CREDENTIALS_H
