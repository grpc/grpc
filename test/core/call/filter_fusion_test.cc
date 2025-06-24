// Copyright 2025 gRPC authors.
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

#include "src/core/call/filter_fusion.h"

#include <grpc/impl/grpc_types.h>

#include <memory>
#include <type_traits>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/lib/transport/transport.h"

using testing::ElementsAre;

namespace grpc_core {
namespace {

// global string to capture operations on calls through this test
// without needing to pass context around
std::vector<std::string> history;

class Test1 : public ImplementChannelFilter<Test1> {
 public:
  static absl::string_view TypeName() { return "Test1"; }
  static absl::StatusOr<std::unique_ptr<Test1>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return std::make_unique<Test1>();
  }
  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata&) {
      history.push_back("Test1::Call::OnClientInitialMetadata");
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    void OnServerToClientMessage(Message&) {
      history.push_back("Test1::Call::OnServerToClientMessage");
    }
    void OnClientToServerHalfClose() {
      history.push_back("Test1::Call::OnClientToServerHalfClose");
    }
    void OnFinalize(const grpc_call_final_info*, Test1* test1) {
      history.push_back("Test1::Call::OnFinalize");
      test1->FilterMethod();
    }
    void OnServerTrailingMetadata(ServerMetadata&) {
      history.push_back("Test1::Call::OnServerTrailingMetadata");
    }

   private:
  };

  void FilterMethod() { history.push_back("Test1::FilterMethod"); }

  bool StartTransportOp(grpc_transport_op* op) override {
    history.push_back("Test1::StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) override {
    history.push_back("Test1::GetChannelInfo");
    return false;
  }
};

class Test2 : public ImplementChannelFilter<Test2> {
 public:
  static absl::string_view TypeName() { return "Test2"; }
  static absl::StatusOr<std::unique_ptr<Test2>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return std::make_unique<Test2>();
  }
  class Call {
   public:
    void OnClientInitialMetadata(const ClientMetadata&) {
      history.push_back("Test2::Call::OnClientInitialMetadata");
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    void OnClientToServerMessage(Message&) {
      history.push_back("Test2::Call::OnClientToServerMessage");
    }
    void OnServerToClientMessage(Message&) {
      history.push_back("Test2::Call::OnServerToClientMessage");
    }
    void OnClientToServerHalfClose() {
      history.push_back("Test2::Call::OnClientToServerHalfClose");
    }
    void OnServerTrailingMetadata(ServerMetadata&, Test2* test2) {
      history.push_back("Test2::Call::OnServerTrailingMetadata");
      test2->FilterMethod();
    }

    static inline const NoInterceptor OnFinalize;

   private:
  };

  void FilterMethod() { history.push_back("Test2::FilterMethod"); }

  bool StartTransportOp(grpc_transport_op* op) override {
    history.push_back("Test2::StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) override {
    history.push_back("Test2::GetChannelInfo");
    return false;
  }
};

class Test3 : public ImplementChannelFilter<Test3> {
 public:
  static absl::string_view TypeName() { return "Test3"; }
  static absl::StatusOr<std::unique_ptr<Test3>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return std::make_unique<Test3>();
  }
  class Call {
   public:
    absl::Status OnClientInitialMetadata(const ClientMetadata&) {
      history.push_back("Test3::Call::OnClientInitialMetadata");
      return absl::OkStatus();
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    absl::StatusOr<MessageHandle> OnClientToServerMessage(MessageHandle handle,
                                                          Test3* test3) {
      history.push_back("Test3::Call::OnClientToServerMessage");
      test3->FilterMethod();
      return handle;
    }
    absl::StatusOr<MessageHandle> OnServerToClientMessage(MessageHandle handle,
                                                          Test3*) {
      history.push_back("Test3::Call::OnServerToClientMessage");
      return handle;
    }
    void OnServerTrailingMetadata(ServerMetadata&) {
      history.push_back("Test3::Call::OnServerTrailingMetadata");
    }
    static inline const NoInterceptor OnClientToServerHalfClose;
    void OnFinalize(const grpc_call_final_info*) {
      history.push_back("Test3::Call::OnFinalize");
    }

   private:
  };

  void FilterMethod() { history.push_back("Test3::FilterMethod"); }

  bool StartTransportOp(grpc_transport_op* op) override {
    history.push_back("Test3::StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) override {
    history.push_back("Test3::GetChannelInfo");
    return false;
  }
};

class Test4 : public ImplementChannelFilter<Test4> {
 public:
  static absl::string_view TypeName() { return "Test4"; }
  static absl::StatusOr<std::unique_ptr<Test4>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return std::make_unique<Test4>();
  }
  class Call {
   public:
    ServerMetadataHandle OnClientInitialMetadata(const ClientMetadata&,
                                                 Test4* test4) {
      history.push_back("Test4::Call::OnClientInitialMetadata");
      test4->FilterMethod();
      return nullptr;
    }
    void OnServerInitialMetadata(ServerMetadata&, Test4*) {
      history.push_back("Test4::Call::OnServerInitialMetadata");
    }
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerTrailingMetadata;
    void OnFinalize(const grpc_call_final_info*, Test4*) {
      history.push_back("Test4::Call::OnFinalize");
    }

   private:
  };

  void FilterMethod() { history.push_back("Test4::FilterMethod"); }

  bool StartTransportOp(grpc_transport_op* op) override {
    history.push_back("Test4::StartTransportOp");
    return true;
  }

  bool GetChannelInfo(const grpc_channel_info* info) override {
    history.push_back("Test4::GetChannelInfo");
    return true;
  }
};

class Test5 : public ImplementChannelFilter<Test5> {
 public:
  static absl::string_view TypeName() { return "Test5"; }
  static absl::StatusOr<std::unique_ptr<Test5>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return std::make_unique<Test5>();
  }
  class Call {
   public:
    explicit Call(Test5* test5) { test5->CalledFromCallConstructor(); }
    void OnClientInitialMetadata(ClientMetadata&, Test5* test5) {
      history.push_back("Test5::Call::OnClientInitialMetadata");
      test5->FilterMethod();
    }
    void OnServerInitialMetadata(ServerMetadata&, Test5* filter) {
      history.push_back("Test5::Call::OnServerInitialMetadata");
    }

    ~Call() { history.push_back("Test5::Call::CalledFromCallDestructor"); }
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnFinalize;

   private:
  };

  void FilterMethod() { history.push_back("Test5::FilterMethod"); }

  void CalledFromCallConstructor() {
    history.push_back("Test5::CalledFromCallConstructor");
  }

  bool StartTransportOp(grpc_transport_op* op) override {
    history.push_back("Test5::StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) override {
    history.push_back("Test5::GetChannelInfo");
    return false;
  }
};

class Test6 : public ImplementChannelFilter<Test5> {
 public:
  static absl::string_view TypeName() { return "Test5"; }
  static absl::StatusOr<std::unique_ptr<Test6>> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return absl::UnimplementedError("Test6 is not implemented");
  }
  class Call {
   public:
    static inline const NoInterceptor OnClientInitialMetadata;
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnFinalize;

   private:
  };

  bool StartTransportOp(grpc_transport_op* op) override {
    LOG(FATAL) << "Test6::StartTransportOp should not be called";
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) override {
    LOG(FATAL) << "Test6::GetChannelInfo should not be called";
    return false;
  }
};

using TestFusedFilter =
    FusedFilter<FilterEndpoint::kClient, 0, Test1, Test2, Test3, Test4, Test5>;
using TestFailedFusedFilter = FusedFilter<FilterEndpoint::kClient, 0, Test1,
                                          Test2, Test3, Test4, Test5, Test6>;

static_assert(
    !std::is_same_v<decltype(&TestFusedFilter::Call::OnClientInitialMetadata),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedFilter::Call::OnServerInitialMetadata),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedFilter::Call::OnClientToServerMessage),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedFilter::Call::OnServerToClientMessage),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedFilter::Call::OnClientToServerHalfClose),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedFilter::Call::OnServerTrailingMetadata),
                    const NoInterceptor*>);
static_assert(!std::is_same_v<decltype(&TestFusedFilter::Call::OnFinalize),
                              const NoInterceptor*>);

template <typename T>
typename ServerMetadataOrHandle<T>::ValueType RunSuccessfulPromise(
    Promise<ServerMetadataOrHandle<T>> promise) {
  for (;;) {
    auto p = promise();
    if (p.ready()) {
      auto value = std::move(p.value());
      CHECK(value.ok()) << value.metadata()->DebugString();
      return std::move(*value);
    }
  }
}

TEST(FusedFilterTest, ClientFilterTest) {
  history.clear();
  absl::StatusOr<std::unique_ptr<TestFusedFilter>> filter =
      TestFusedFilter::Create(ChannelArgs(), ChannelFilter::Args());
  CHECK(filter.ok());
  {
    TestFusedFilter::Call call((*filter).get());
    auto message = Arena::MakePooled<Message>();
    auto server_metadata_handle = Arena::MakePooled<ServerMetadata>();
    auto server_trailing_metadata_handle = Arena::MakePooled<ServerMetadata>();
    auto client_metadata_handle = Arena::MakePooled<ClientMetadata>();
    struct grpc_call_final_info info;
    message = RunSuccessfulPromise<Message>(
        call.OnClientToServerMessage(std::move(message), (*filter).get()));
    RunSuccessfulPromise<Message>(
        call.OnServerToClientMessage(std::move(message), (*filter).get()));
    RunSuccessfulPromise<ServerMetadata>(call.OnServerInitialMetadata(
        std::move(server_metadata_handle), (*filter).get()));
    RunSuccessfulPromise<ClientMetadata>(call.OnClientInitialMetadata(
        std::move(client_metadata_handle), (*filter).get()));
    call.OnServerTrailingMetadata(*server_trailing_metadata_handle,
                                  (*filter).get());
    call.OnClientToServerHalfClose();
    call.OnFinalize(&info, (*filter).get());
  }
  EXPECT_THAT(
      history,
      ElementsAre(
          "Test5::CalledFromCallConstructor",
          "Test2::Call::OnClientToServerMessage",
          "Test3::Call::OnClientToServerMessage", "Test3::FilterMethod",
          // ServerToClientMessage execution order must be reversed.
          "Test3::Call::OnServerToClientMessage",
          "Test2::Call::OnServerToClientMessage",
          "Test1::Call::OnServerToClientMessage",
          // ServerInitialMetadata execution order must be reversed.
          "Test5::Call::OnServerInitialMetadata",
          "Test4::Call::OnServerInitialMetadata",
          "Test1::Call::OnClientInitialMetadata",
          "Test2::Call::OnClientInitialMetadata",
          "Test3::Call::OnClientInitialMetadata",
          "Test4::Call::OnClientInitialMetadata", "Test4::FilterMethod",
          "Test5::Call::OnClientInitialMetadata", "Test5::FilterMethod",
          // ServerTrailingMetadata execution order must be reversed.
          "Test3::Call::OnServerTrailingMetadata",
          "Test2::Call::OnServerTrailingMetadata", "Test2::FilterMethod",
          "Test1::Call::OnServerTrailingMetadata",
          "Test1::Call::OnClientToServerHalfClose",
          "Test2::Call::OnClientToServerHalfClose", "Test1::Call::OnFinalize",
          "Test1::FilterMethod", "Test3::Call::OnFinalize",
          "Test4::Call::OnFinalize", "Test5::Call::CalledFromCallDestructor"));
  history.clear();
  grpc_transport_op op;
  grpc_channel_info channel_info;
  EXPECT_TRUE((*filter)->StartTransportOp(&op));
  EXPECT_TRUE((*filter)->GetChannelInfo(&channel_info));
  EXPECT_THAT(history,
              ElementsAre("Test1::StartTransportOp", "Test2::StartTransportOp",
                          "Test3::StartTransportOp", "Test4::StartTransportOp",
                          "Test1::GetChannelInfo", "Test2::GetChannelInfo",
                          "Test3::GetChannelInfo", "Test4::GetChannelInfo"));
}

TEST(FusedFilterTest, FusedFilterTypeName) {
  EXPECT_EQ(TestFusedFilter::TypeName(), "Test1+Test2+Test3+Test4+Test5");
}

// Assert that the fused filter creation fails when one of the filters creation
// fails.
TEST(FusedFilterTest, FailedFusedFilter) {
  EXPECT_FALSE(
      TestFailedFusedFilter::Create(ChannelArgs(), ChannelFilter::Args()).ok());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
