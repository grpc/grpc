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

#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;

namespace grpc_core {
namespace {

// global string to capture operations on calls through this test
// without needing to pass context around
std::vector<std::string> history;

class Test1 {
 public:
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

   private:
  };
};

class Test2 {
 public:
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

   private:
  };
};

class Test3 {
 public:
  class Call {
   public:
    absl::Status OnClientInitialMetadata(const ClientMetadata&) {
      history.push_back("Test3::Call::OnClientInitialMetadata");
      return absl::OkStatus();
    }
    static inline const NoInterceptor OnServerInitialMetadata;
    absl::StatusOr<MessageHandle> OnClientToServerMessage(MessageHandle handle,
                                                          Test3*) {
      history.push_back("Test3::Call::OnClientToServerMessage");
      return handle;
    }
    absl::StatusOr<MessageHandle> OnServerToClientMessage(MessageHandle handle,
                                                          Test3*) {
      history.push_back("Test3::Call::OnServerToClientMessage");
      return handle;
    }

   private:
  };
};

class Test4 {
 public:
  class Call {
   public:
    ServerMetadataHandle OnClientInitialMetadata(const ClientMetadata&,
                                                 Test4* filter) {
      history.push_back("Test4::Call::OnClientInitialMetadata");
      return nullptr;
    }
    void OnServerInitialMetadata(ServerMetadata&, Test4* filter) {
      history.push_back("Test4::Call::OnServerInitialMetadata");
    }
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnServerToClientMessage;

   private:
  };
};

class Test5 {
 public:
  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata&, Test5* filter) {
      history.push_back("Test5::Call::OnClientInitialMetadata");
    }
    void OnServerInitialMetadata(ServerMetadata&, Test5* filter) {
      history.push_back("Test5::Call::OnServerInitialMetadata");
    }
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnServerToClientMessage;

   private:
  };
};

using Test123 = FusedFilter<Test1, Test2, Test3, Test4, Test5>;

static_assert(!std::is_same_v<decltype(&Test123::Call::OnClientInitialMetadata),
                              const NoInterceptor*>);
static_assert(!std::is_same_v<decltype(&Test123::Call::OnServerInitialMetadata),
                              const NoInterceptor*>);
static_assert(!std::is_same_v<decltype(&Test123::Call::OnClientToServerMessage),
                              const NoInterceptor*>);
static_assert(!std::is_same_v<decltype(&Test123::Call::OnServerToClientMessage),
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

TEST(Test123, OrderCorrect) {
  Test123 filter;
  Test123::Call call;
  history.clear();
  auto message = Arena::MakePooled<Message>();
  auto server_metadata_handle = Arena::MakePooled<ServerMetadata>();
  auto client_metadata_handle = Arena::MakePooled<ClientMetadata>();
  message = RunSuccessfulPromise<Message>(
      call.OnClientToServerMessage(std::move(message), &filter));
  RunSuccessfulPromise<Message>(
      call.OnServerToClientMessage(std::move(message), &filter));
  RunSuccessfulPromise<ServerMetadata>(
      call.OnServerInitialMetadata(std::move(server_metadata_handle), &filter));
  RunSuccessfulPromise<ClientMetadata>(
      call.OnClientInitialMetadata(std::move(client_metadata_handle), &filter));
  EXPECT_THAT(history, ElementsAre("Test2::Call::OnClientToServerMessage",
                                   "Test3::Call::OnClientToServerMessage",
                                   "Test3::Call::OnServerToClientMessage",
                                   "Test2::Call::OnServerToClientMessage",
                                   "Test1::Call::OnServerToClientMessage",
                                   "Test5::Call::OnServerInitialMetadata",
                                   "Test4::Call::OnServerInitialMetadata",
                                   "Test1::Call::OnClientInitialMetadata",
                                   "Test2::Call::OnClientInitialMetadata",
                                   "Test3::Call::OnClientInitialMetadata",
                                   "Test4::Call::OnClientInitialMetadata",
                                   "Test5::Call::OnClientInitialMetadata"));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
