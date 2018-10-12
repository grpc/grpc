/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpcpp/impl/codegen/client_interceptor.h>

namespace grpc {
namespace experimental {
/*const ClientRpcInfo::grpc::InterceptedMessage& outgoing_message() {}
grpc::InterceptedMessage* ClientRpcInfo::mutable_outgoing_message() {}
const grpc::InterceptedMessage& ClientRpcInfo::received_message() {}
grpc::InterceptedMessage* ClientRpcInfo::mutable_received_message() {}
const Status ClientRpcInfo::*status() {}

// Setter methods
template <class M>
void ClientRpcInfo::set_outgoing_message(M* msg) {}  // edit outgoing message
template <class M>
void ClientRpcInfo::set_received_message(M* msg) {}  // edit received message
// for hijacking (can be called multiple times for streaming)
template <class M>
void ClientRpcInfo::inject_received_message(M* msg) {}
void set_client_initial_metadata(
    const ClientRpcInfo::std::multimap<grpc::string, grpc::string>& overwrite) {
}
void ClientRpcInfo::set_server_initial_metadata(
    const std::multimap<grpc::string, grpc::string>& overwrite) {}
void ClientRpcInfo::set_server_trailing_metadata(
    const std::multimap<grpc::string, grpc::string>& overwrite) {}
void ClientRpcInfo::set_status(Status status) {}*/
}  // namespace experimental
}  // namespace grpc
