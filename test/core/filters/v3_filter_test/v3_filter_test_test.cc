// Copyright 2026 gRPC authors.
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

// Tests for the FilterTestV3 harness itself.
//
// Using synthetic filters defined right here, so every assertion
// is about the harness plumbing rather than any particular filter's behavior:
//
//   * Happy path: all six call lifecycle events fire end-to-end through a
//     transparent filter on the real CallSpine.
//   * Failure path: a filter that aborts a call at its first interception point
//     terminates it before the server, and the client sees that status.
//   * Composition: a chain of filters runs in Add() order.

#include "test/core/filters/v3_filter_test/v3_filter_test.h"

#include <grpc/status.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/channelz/property_list.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/filters/filter_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {

namespace {

// A filter that intercepts nothing: every event passes through untouched. It
// exists solely to place a real filter in the call path so the harness
// exercises the genuine CallFilters executor.
class PassThroughFilter : public ImplementChannelFilter<PassThroughFilter> {
 public:
  static absl::string_view TypeName() { return "pass_through"; }

  static absl::StatusOr<std::unique_ptr<PassThroughFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<PassThroughFilter>();
  }

  class Call {
   public:
    static inline const NoInterceptor OnClientInitialMetadata;
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() { return {}; }
  };
};

// A filter that rejects every call at initial metadata.
class RejectingFilter : public ImplementChannelFilter<RejectingFilter> {
 public:
  static absl::string_view TypeName() { return "rejecting"; }

  static absl::StatusOr<std::unique_ptr<RejectingFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<RejectingFilter>();
  }

  class Call {
   public:
    ServerMetadataHandle OnClientInitialMetadata(ClientMetadata&) {
      return ServerMetadataFromStatus(GRPC_STATUS_PERMISSION_DENIED,
                                      "rejected by filter");
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() { return {}; }
  };
};

// Appends `tag` to the "chain-order" header. Duplicate values of one unknown
// key are joined with "," in append order, so a chain of StampingFilters leaves
// behind a record of exactly which filters ran, and in what order.
void StampChainOrder(ClientMetadata& md, absl::string_view tag) {
  md.Append("chain-order", Slice::FromCopiedString(tag),
            [](absl::string_view, const Slice&) {});
}

class FirstFilter : public ImplementChannelFilter<FirstFilter> {
 public:
  static absl::string_view TypeName() { return "first"; }

  static absl::StatusOr<std::unique_ptr<FirstFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<FirstFilter>();
  }

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md) {
      StampChainOrder(md, "first");
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() { return {}; }
  };
};

class SecondFilter : public ImplementChannelFilter<SecondFilter> {
 public:
  static absl::string_view TypeName() { return "second"; }

  static absl::StatusOr<std::unique_ptr<SecondFilter>> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return std::make_unique<SecondFilter>();
  }

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& md) {
      StampChainOrder(md, "second");
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() { return {}; }
  };
};

}  // namespace

// A unary echo through a pass-through filter: client sends initial metadata + a
// message + half-close; server replies with initial metadata + an echoed
// message + OK trailers. Exercises OnClientInitialMetadata,
// OnClientToServerMessage, OnClientToServerHalfClose, OnServerInitialMetadata,
// OnServerToClientMessage and OnServerTrailingMetadata across the real spine.
//
FILTER_TEST_V3(UnaryEchoThroughPassThroughFilter) {
  ASSERT_TRUE(Add<PassThroughFilter>().Build().ok());

  // "echo-test" is a custom key with no metadata trait, so it must be appended
  // by name. Unknown keys are never parsed, so the error callback is
  // unreachable; the test asserts the key arrives intact below regardless.
  auto client_initial_metadata =
      Arena::MakePooledForOverwrite<ClientMetadata>();
  client_initial_metadata->Append("echo-test", Slice::FromStaticString("on"),
                                  [](absl::string_view, const Slice&) {});
  auto [initiator, handler] = StartCall(std::move(client_initial_metadata));
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello")), 0));
      },
      [initiator = initiator](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [initiator = initiator](
          ValueOrFailure<std::optional<ServerMetadataHandle>> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_THAT(***md, HasMetadataKeyValue("server-hdr", "yes"));
        return initiator.PullMessage();
      },
      [initiator = initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_THAT(msg.value(), HasMessagePayload("hello"));
        return initiator.PullMessage();
      },
      [initiator = initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // end of server->client stream
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  auto echoed = std::make_shared<std::string>();
  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable {
        return handler.PullClientInitialMetadata();
      },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataKeyValue("echo-test", "on"));
        // Pull the client message *before* producing any server output.
        return handler.PullMessage();
      },
      [handler = handler, echoed](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_THAT(msg.value(), HasMessagePayload("hello"));
        *echoed = msg.value().payload()->JoinIntoString();
        auto server_initial_metadata =
            Arena::MakePooledForOverwrite<ServerMetadata>();
        server_initial_metadata->Append("server-hdr",
                                        Slice::FromStaticString("yes"),
                                        [](absl::string_view, const Slice&) {});
        return handler.PushServerInitialMetadata(
            std::move(server_initial_metadata));
      },
      [handler = handler, echoed](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString(*echoed)), 0));
      },
      [handler = handler](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PullMessage();
      },
      [handler = handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // client half-closed
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

// Failure path: a filter that aborts a call at its very first interception
// point. Returning a ServerMetadataHandle from OnClientInitialMetadata
// terminates the call before it reaches the server, so the client observes the
// filter's status directly. This is the cleanest form of the "filter rejects a
// call" contract and is robust under any fuzzed schedule (there is no
// server-side work to drain).
FILTER_TEST_V3(FilterRejectsAtInitialMetadata) {
  ASSERT_TRUE(Add<RejectingFilter>().Build().ok());

  auto [initiator, handler] =
      StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator]() mutable {
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::PermissionDeniedError(
                              "rejected by filter")));
      });

  // Filters only execute as the server pulls: OnClientInitialMetadata runs when
  // the handler pulls the client initial metadata. Drive that pull so the
  // filter fires; because the filter aborts, the pull resolves to failure.
  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable {
        return handler.PullClientInitialMetadata();
      },
      [](ValueOrFailure<ClientMetadataHandle> md) { EXPECT_FALSE(md.ok()); });

  WaitForAllPendingWork();
}

// Composition: filters run in the order they were Add()ed, and every filter in
// the chain sees the call. Each filter stamps its own tag onto one header, so
// the value the server ends up with spells out the order they ran in.
FILTER_TEST_V3(FiltersRunInAddOrder) {
  ASSERT_TRUE(Add<FirstFilter>().Add<SecondFilter>().Build().ok());

  auto [initiator, handler] =
      StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator]() mutable {
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable {
        return handler.PullClientInitialMetadata();
      },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        ASSERT_TRUE(md.ok());
        std::string backing;
        EXPECT_EQ((**md).GetStringValue("chain-order", &backing),
                  "first,second");
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
