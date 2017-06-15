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

#ifndef GRPCXX_IMPL_CODEGEN_SERIALIZATION_TRAITS_H
#define GRPCXX_IMPL_CODEGEN_SERIALIZATION_TRAITS_H

namespace grpc {

/// Defines how to serialize and deserialize some type.
///
/// Used for hooking different message serialization API's into GRPC.
/// Each SerializationTraits implementation must provide the following
/// functions:
///   static Status Serialize(const Message& msg,
///                           grpc_byte_buffer** buffer,
///                           bool* own_buffer);
///   static Status Deserialize(grpc_byte_buffer* buffer,
///                             Message* msg,
///                             int max_receive_message_size);
///
/// Serialize is required to convert message to a grpc_byte_buffer, and
/// to store a pointer to that byte buffer at *buffer. *own_buffer should
/// be set to true if the caller owns said byte buffer, or false if
/// ownership is retained elsewhere.
///
/// Deserialize is required to convert buffer into the message stored at
/// msg. max_receive_message_size is passed in as a bound on the maximum
/// number of message bytes Deserialize should accept.
///
/// Both functions return a Status, allowing them to explain what went
/// wrong if required.
template <class Message,
          class UnusedButHereForPartialTemplateSpecialization = void>
class SerializationTraits;

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SERIALIZATION_TRAITS_H
