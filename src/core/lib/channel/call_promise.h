// Copyright 2022 gRPC authors.
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

#ifndef CALL_PROMISE_H
#define CALL_PROMISE_H

#include <type_traits>

#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/seq.h"

namespace grpc_core {

namespace call_promise_detail {

template <FilterEndpoint kEndpoint, typename OnServerInitialMetadataType,
          typename MapOutgoingMessageType, typename MapIncomingMessageType,
          typename NextPromiseFactory, typename Void = void>
class MainLoop;

template <FilterEndpoint kEndpoint, typename NextPromiseFactory>
class MainLoop<kEndpoint, Empty, Empty, Empty, NextPromiseFactory> {
 public:
  static auto MakePromise(CallArgs call_args, Empty, Empty, Empty, Empty,
                          NextPromiseFactory f) {
    return f(std::move(call_args));
  }
};

template <typename Result>
class OnServerInitialMetadataHandler;

inline auto WrapServerMetadataInHandle(ServerMetadata** p) {
  return ServerMetadataHandle(*p, Arena::PooledDeleter(nullptr));
}

template <>
class OnServerInitialMetadataHandler<absl::Status> {
 public:
  template <typename OnServerInitialMetadataType>
  static auto Wrap(OnServerInitialMetadataType f, CallArgs& args) {
    return Map(args.server_initial_metadata->Wait(),
               [f = std::move(f)](ServerMetadata** p) {
                 return f(WrapServerMetadataInHandle(p));
               });
  }
};

template <>
class OnServerInitialMetadataHandler<ServerMetadataHandle> {
 public:
  template <typename OnServerInitialMetadataType>
  static auto Wrap(OnServerInitialMetadataType f, CallArgs& args) {
    auto* read_latch = GetContext<Arena>()->New<Latch<ServerMetadata*>>();
    auto* write_latch = std::exchange(args.server_initial_metadata, read_latch);
    return Seq(
        Map(read_latch->Wait(), WrapServerMetadataInHandle),
        Map(std::move(f), [read_latch, write_latch](ServerMetadataHandle h) {
          if (h.get() != read_latch->Get()) {
            *read_latch->Get() = std::move(*h.get());
          }
          write_latch->Set(read_latch->Get());
          return absl::OkStatus();
        }));
  }
};

template <FilterEndpoint kEndpoint, typename OnServerInitialMetadataType,
          typename NextPromiseFactory>
class MainLoop<kEndpoint, OnServerInitialMetadataType, Empty, Empty,
               NextPromiseFactory> {
 public:
  static auto MakePromise(
      CallArgs call_args,
      OnServerInitialMetadataType on_server_initial_metadata, Empty, Empty,
      NextPromiseFactory f) {
    auto wrapped_on_server_initial_metadata =
        OnServerInitialMetadataHandler<decltype(on_server_initial_metadata(
            std::declval<ServerMetadataHandle>()))>::
            Wrap(std::move(on_server_initial_metadata), call_args);
    return TryConcurrently(f(std::move(call_args)))
        .NecessaryPull(wrapped_on_server_initial_metadata);
  }
};

template <typename OnClientInitialMetadataType,
          typename OnServerTrailingMetadataType>
class AddBracketingMetadata {
 public:
  template <typename Middle>
  static auto MakePromise(OnClientInitialMetadataType start, Middle middle,
                          OnServerTrailingMetadataType end) {
    return Seq(std::move(start), std::move(middle), std::move(end));
  }
};

template <typename OnServerTrailingMetadataType>
class AddBracketingMetadata<Empty, OnServerTrailingMetadataType> {
 public:
  template <typename Middle>
  static auto MakePromise(Empty, Middle middle,
                          OnServerTrailingMetadataType end) {
    return Seq(std::move(middle), std::move(end));
  }
};

template <typename OnClientInitialMetadataType>
class AddBracketingMetadata<OnClientInitialMetadataType, Empty> {
 public:
  template <typename Middle>
  static auto MakePromise(OnClientInitialMetadataType start, Middle middle,
                          Empty) {
    return Seq(std::move(start), std::move(middle));
  }
};

template <>
class AddBracketingMetadata<Empty, Empty> {
 public:
  template <typename Middle>
  static auto MakePromise(Empty, Middle middle, Empty) {
    return middle;
  }
};

template <typename OnClientInitialMetadataType,
          typename OnServerInitialMetadataType,
          typename OnServerTrailingMetadataType,
          typename MapOutgoingMessageType, typename MapIncomingMessageType>
class CallPromiseBuilder {
 public:
  CallPromiseBuilder() = default;

  CallPromiseBuilder(OnClientInitialMetadataType on_client_initial_metadata,
                     OnServerInitialMetadataType on_server_initial_metadata,
                     OnServerTrailingMetadataType on_server_trailing_metadata,
                     MapOutgoingMessageType map_outgoing_message,
                     MapIncomingMessageType map_incoming_message)
      : on_client_initial_metadata_(std::move(on_client_initial_metadata)),
        on_server_initial_metadata_(std::move(on_server_initial_metadata)),
        on_server_trailing_metadata_(std::move(on_server_trailing_metadata)),
        map_outgoing_message_(std::move(map_outgoing_message)),
        map_incoming_message_(std::move(map_incoming_message)) {}

  template <typename F>
  CallPromiseBuilder<F, OnServerInitialMetadataType,
                     OnServerTrailingMetadataType, MapOutgoingMessageType,
                     MapIncomingMessageType>
  OnClientInitialMetadata(F f) {
    static_assert(std::is_same<OnClientInitialMetadataType, Empty>::value,
                  "OnClientInitialMetadata already set");
    return CallPromiseBuilder<F, OnServerInitialMetadataType,
                              OnServerTrailingMetadataType,
                              MapOutgoingMessageType, MapIncomingMessageType>{
        std::forward<F>(f), on_server_initial_metadata_,
        on_server_trailing_metadata_, map_outgoing_message_,
        map_incoming_message_};
  }

  template <typename F>
  CallPromiseBuilder<OnClientInitialMetadataType, F,
                     OnServerTrailingMetadataType, MapOutgoingMessageType,
                     MapIncomingMessageType>
  OnServerInitialMetadata(F f) {
    static_assert(std::is_same<OnServerInitialMetadataType, Empty>::value,
                  "OnServerInitialMetadata already set");
    return CallPromiseBuilder<OnClientInitialMetadataType, F,
                              OnServerTrailingMetadataType,
                              MapOutgoingMessageType, MapIncomingMessageType>{
        on_client_initial_metadata_, std::forward<F>(f),
        on_server_trailing_metadata_, map_outgoing_message_,
        map_incoming_message_};
  }

  template <typename F>
  CallPromiseBuilder<OnClientInitialMetadataType, OnServerInitialMetadataType,
                     F, MapOutgoingMessageType, MapIncomingMessageType>
  OnServerTrailingMetadata(F f) {
    static_assert(std::is_same<OnServerTrailingMetadataType, Empty>::value,
                  "OnServerTrailingMetadata already set");
    return CallPromiseBuilder<OnClientInitialMetadataType,
                              OnServerInitialMetadataType, F,
                              MapOutgoingMessageType, MapIncomingMessageType>{
        on_client_initial_metadata_, on_server_initial_metadata_,
        std::forward<F>(f), map_outgoing_message_, map_incoming_message_};
  }

  template <typename F>
  CallPromiseBuilder<OnClientInitialMetadataType, OnServerInitialMetadataType,
                     OnServerTrailingMetadataType, F, MapIncomingMessageType>
  MapOutgoingMessage(F f) {
    static_assert(std::is_same<MapOutgoingMessageType, Empty>::value,
                  "MapOutgoingMessage already set");
    return CallPromiseBuilder<
        OnClientInitialMetadataType, OnServerInitialMetadataType,
        OnServerTrailingMetadataType, F, MapIncomingMessageType>{
        on_client_initial_metadata_, on_server_initial_metadata_,
        on_server_trailing_metadata_, std::forward<F>(f),
        map_incoming_message_};
  }

  template <typename F>
  CallPromiseBuilder<OnClientInitialMetadataType, OnServerInitialMetadataType,
                     OnServerTrailingMetadataType, MapOutgoingMessageType, F>
  MapIncomingMessage(F f) {
    static_assert(std::is_same<MapIncomingMessageType, Empty>::value,
                  "MapIncomingMessage already set");
    return CallPromiseBuilder<
        OnClientInitialMetadataType, OnServerInitialMetadataType,
        OnServerTrailingMetadataType, MapOutgoingMessageType, F>{
        on_client_initial_metadata_, on_server_initial_metadata_,
        on_server_trailing_metadata_, map_outgoing_message_,
        std::forward<F>(f)};
  }

  auto BuildClient(CallArgs call_args,
                   NextPromiseFactory next_promise_factory) {
    return AddBracketingMetadata<OnClientInitialMetadataType,
                                 OnServerTrailingMetadataType>::
        MakePromise(
            std::move(on_client_initial_metadata_),
            MainLoop<FilterEndpoint::kClient, OnServerInitialMetadataType,
                     MapOutgoingMessageType, MapIncomingMessageType,
                     NextPromiseFactory>::
                MakePromise(std::move(call_args),
                            std::move(on_server_initial_metadata_),
                            std::move(map_outgoing_message_),
                            std::move(map_incoming_message_),
                            std::move(next_promise_factory)),
            std::move(on_server_trailing_metadata_));
  }

  auto BuildServer(CallArgs call_args,
                   NextPromiseFactory next_promise_factory) {
    return AddBracketingMetadata<OnClientInitialMetadataType,
                                 OnServerTrailingMetadataType>::
        MakePromise(
            std::move(on_client_initial_metadata_),
            MainLoop<FilterEndpoint::kServer, OnServerInitialMetadataType,
                     MapOutgoingMessageType, MapIncomingMessageType,
                     NextPromiseFactory>::
                MakePromise(std::move(call_args),
                            std::move(on_server_initial_metadata_),
                            std::move(map_outgoing_message_),
                            std::move(map_incoming_message_),
                            std::move(next_promise_factory)),
            std::move(on_server_trailing_metadata_));
  }

 private:
  GPR_NO_UNIQUE_ADDRESS OnClientInitialMetadataType on_client_initial_metadata_;
  GPR_NO_UNIQUE_ADDRESS OnServerInitialMetadataType on_server_initial_metadata_;
  GPR_NO_UNIQUE_ADDRESS OnServerTrailingMetadataType
      on_server_trailing_metadata_;
  GPR_NO_UNIQUE_ADDRESS MapOutgoingMessageType map_outgoing_message_;
  GPR_NO_UNIQUE_ADDRESS MapIncomingMessageType map_incoming_message_;
};

}  // namespace call_promise_detail

using CallPromiseBuilder =
    call_promise_detail::CallPromiseBuilder<Empty, Empty, Empty, Empty, Empty>;

}  // namespace grpc_core

#endif
