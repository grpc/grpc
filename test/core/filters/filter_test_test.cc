// Copyright 2023 gRPC authors.
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

#include "test/core/filters/filter_test.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

using ::testing::_;
using ::testing::StrictMock;

namespace grpc_core {
namespace {

class NoOpFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    return next(std::move(args));
  }

  static absl::StatusOr<NoOpFilter> Create(const ChannelArgs&,
                                           ChannelFilter::Args) {
    return NoOpFilter();
  }
};
using NoOpFilterTest = FilterTest<NoOpFilter>;

class DelayStartFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    return Seq(
        [args = std::move(args), i = 10]() mutable -> Poll<CallArgs> {
          --i;
          if (i == 0) return std::move(args);
          Activity::current()->ForceImmediateRepoll();
          return Pending{};
        },
        next);
  }

  static absl::StatusOr<DelayStartFilter> Create(const ChannelArgs&,
                                                 ChannelFilter::Args) {
    return DelayStartFilter();
  }
};
using DelayStartFilterTest = FilterTest<DelayStartFilter>;

class AddClientInitialMetadataFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    args.client_initial_metadata->Set(HttpPathMetadata(),
                                      Slice::FromCopiedString("foo.bar"));
    return next(std::move(args));
  }

  static absl::StatusOr<AddClientInitialMetadataFilter> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return AddClientInitialMetadataFilter();
  }
};
using AddClientInitialMetadataFilterTest =
    FilterTest<AddClientInitialMetadataFilter>;

class AddServerTrailingMetadataFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    return Map(next(std::move(args)), [](ServerMetadataHandle handle) {
      handle->Set(HttpStatusMetadata(), 420);
      return handle;
    });
  }

  static absl::StatusOr<AddServerTrailingMetadataFilter> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return AddServerTrailingMetadataFilter();
  }
};
using AddServerTrailingMetadataFilterTest =
    FilterTest<AddServerTrailingMetadataFilter>;

class AddServerInitialMetadataFilter final : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs args, NextPromiseFactory next) override {
    args.server_initial_metadata->InterceptAndMap([](ServerMetadataHandle md) {
      md->Set(GrpcEncodingMetadata(), GRPC_COMPRESS_GZIP);
      return md;
    });
    return next(std::move(args));
  }

  static absl::StatusOr<AddServerInitialMetadataFilter> Create(
      const ChannelArgs&, ChannelFilter::Args) {
    return AddServerInitialMetadataFilter();
  }
};
using AddServerInitialMetadataFilterTest =
    FilterTest<AddServerInitialMetadataFilter>;

TEST_F(NoOpFilterTest, NoOp) {}

TEST_F(NoOpFilterTest, MakeCall) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
}

TEST_F(NoOpFilterTest, MakeClientMetadata) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  auto md = call.NewClientMetadata({{":path", "foo.bar"}});
  EXPECT_EQ(md->get_pointer(HttpPathMetadata())->as_string_view(), "foo.bar");
}

TEST_F(NoOpFilterTest, MakeServerMetadata) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  auto md = call.NewServerMetadata({{":status", "200"}});
  EXPECT_EQ(md->get(HttpStatusMetadata()), HttpStatusMetadata::ValueType(200));
}

TEST_F(NoOpFilterTest, CanStart) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.Step();
}

TEST_F(DelayStartFilterTest, CanStartWithDelay) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.Step();
}

TEST_F(NoOpFilterTest, CanCancel) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.Cancel();
}

TEST_F(DelayStartFilterTest, CanCancelWithDelay) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  call.Start(call.NewClientMetadata());
  call.Cancel();
}

TEST_F(AddClientInitialMetadataFilterTest, CanSetClientInitialMetadata) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(HasMetadataKeyValue(":path", "foo.bar")));
  call.Start(call.NewClientMetadata());
  call.Step();
}

TEST_F(NoOpFilterTest, CanFinish) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata());
  EXPECT_CALL(call, Finished(_));
  call.Step();
}

TEST_F(AddServerTrailingMetadataFilterTest, CanSetServerTrailingMetadata) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.FinishNextFilter(call.NewServerMetadata());
  EXPECT_CALL(call, Finished(HasMetadataKeyValue(":status", "420")));
  call.Step();
}

TEST_F(NoOpFilterTest, CanProcessServerInitialMetadata) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.ForwardServerInitialMetadata(call.NewServerMetadata());
  EXPECT_CALL(call, ForwardedServerInitialMetadata(_));
  call.Step();
}

TEST_F(AddServerInitialMetadataFilterTest, CanSetServerInitialMetadata) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.ForwardServerInitialMetadata(call.NewServerMetadata());
  EXPECT_CALL(call, ForwardedServerInitialMetadata(
                        HasMetadataKeyValue("grpc-encoding", "gzip")));
  call.Step();
}

TEST_F(NoOpFilterTest, CanProcessClientToServerMessage) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.ForwardMessageClientToServer(call.NewMessage("abc"));
  EXPECT_CALL(call, ForwardedMessageClientToServer(HasMessagePayload("abc")));
  call.Step();
}

TEST_F(NoOpFilterTest, CanProcessServerToClientMessage) {
  StrictMock<Call> call(MakeChannel(ChannelArgs()).value());
  EXPECT_CALL(call, Started(_));
  call.Start(call.NewClientMetadata());
  call.ForwardServerInitialMetadata(call.NewServerMetadata());
  call.ForwardMessageServerToClient(call.NewMessage("abc"));
  EXPECT_CALL(call, ForwardedServerInitialMetadata(_));
  EXPECT_CALL(call, ForwardedMessageServerToClient(HasMessagePayload("abc")));
  call.Step();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
