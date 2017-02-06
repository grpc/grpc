/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
