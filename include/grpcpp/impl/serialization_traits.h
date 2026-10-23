//
//
// Copyright 2015 gRPC authors.
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
//
//

#ifndef GRPCPP_IMPL_SERIALIZATION_TRAITS_H
#define GRPCPP_IMPL_SERIALIZATION_TRAITS_H

#include <grpc/event_engine/memory_allocator.h>

#include <type_traits>
#include <utility>

namespace grpc {

/// Defines how to serialize and deserialize some type.
///
/// Used for hooking different message serialization API's into GRPC.
/// Each SerializationTraits<Message> implementation must provide the
/// following functions:
/// 1.  static Status Serialize(const Message& msg,
///                             ByteBuffer* buffer,
///                             bool* own_buffer);
///     OR
///     static Status Serialize(const Message& msg,
///                             grpc_byte_buffer** buffer,
///                             bool* own_buffer);
///     The former is preferred; the latter is deprecated
///
/// 2.  static Status Deserialize(ByteBuffer* buffer,
///                               Message* msg);
///     OR
///     static Status Deserialize(grpc_byte_buffer* buffer,
///                               Message* msg);
///     The former is preferred; the latter is deprecated
///
/// Serialize is required to convert message to a ByteBuffer, and
/// return that byte buffer through *buffer. *own_buffer should
/// be set to true if the caller owns said byte buffer, or false if
/// ownership is retained elsewhere.
///
/// Deserialize is required to convert buffer into the message stored at
/// msg. max_receive_message_size is passed in as a bound on the maximum
/// number of message bytes Deserialize should accept. Deserialize implementations
/// are responsible for freeing the ByteBuffer by calling buffer->Clear().
///
/// Both functions return a Status, allowing them to explain what went
/// wrong if required.
template <class Message,
          class UnusedButHereForPartialTemplateSpecialization = void>
class SerializationTraits;

namespace impl {

// Helper trait to robustly check if the allocator-aware Serialize exists.
template <typename Message, typename BufferPtr, typename = void>
struct has_allocator_serialize : std::false_type {};

template <typename Message, typename BufferPtr>
struct has_allocator_serialize<
    Message, BufferPtr,
    std::void_t<decltype(SerializationTraits<Message>::Serialize(
        std::declval<grpc_event_engine::experimental::MemoryAllocator*>(),
        std::declval<const Message&>(), std::declval<BufferPtr>(),
        std::declval<bool*>()))>> : std::true_type {};

template <typename Message, typename BufferPtr>
inline constexpr bool has_allocator_serialize_v =
    has_allocator_serialize<Message, BufferPtr>::value;

// Primary template for SerializeDispatch, now switched by a bool.
template <typename Message, typename BufferPtr,
          bool = has_allocator_serialize_v<Message, BufferPtr>>
struct SerializeDispatch;

// Specialization for when the allocator-aware Serialize *exists*.
template <typename Message, typename BufferPtr>
struct SerializeDispatch<Message, BufferPtr, true> {
  static auto Serialize(
      grpc_event_engine::experimental::MemoryAllocator* allocator,
      const Message& msg, BufferPtr buffer, bool* own_buffer) {
    return SerializationTraits<Message>::Serialize(allocator, msg, buffer,
                                                   own_buffer);
  }
};

// Specialization for when the allocator-aware Serialize *does not* exist.
template <typename Message, typename BufferPtr>
struct SerializeDispatch<Message, BufferPtr, false> {
  static auto Serialize(
      grpc_event_engine::experimental::MemoryAllocator* /*allocator*/,
      const Message& msg, BufferPtr buffer, bool* own_buffer) {
    // Fallback to the old Serialize, ignoring the allocator.
    return SerializationTraits<Message>::Serialize(msg, buffer, own_buffer);
  }
};

}  // namespace impl

template <typename Message, typename BufferPtr>
auto Serialize(grpc_event_engine::experimental::MemoryAllocator* allocator,
               const Message& msg, BufferPtr buffer, bool* own_buffer) {
  return impl::SerializeDispatch<Message, BufferPtr>::Serialize(
      allocator, msg, buffer, own_buffer);
}

template <typename BufferPtr, typename Message>
auto Deserialize(BufferPtr buffer, Message* msg) {
  return SerializationTraits<Message>::Deserialize(buffer, msg);
}

}  // namespace grpc

#endif  // GRPCPP_IMPL_SERIALIZATION_TRAITS_H
