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

#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/transport/call_final_info.h"
#include "src/core/lib/transport/transport.h"

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
    void OnClientToServerHalfClose(Test1*) {
      history.push_back("Test1::Call::OnClientToServerHalfClose");
    }
    void OnFinalize(grpc_call_final_info*, Test1*) {
      history.push_back("Test1::Call::OnFinalize");
    }
    void OnServerTrailingMetadata(ServerMetadata&) {
      history.push_back("Test1::Call::OnServerTrailingMetaData");
    }

   private:
  };

  bool StartTransportOp(grpc_transport_op* op) {
    history.push_back("Test1::StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) {
    history.push_back("Test1::GetChannelInfo");
    return false;
  }
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
    void OnClientToServerHalfClose() {
      history.push_back("Test2::Call::OnClientToServerHalfClose");
    }
    void OnServerTrailingMetadata(ServerMetadata&, Test2*) {
      history.push_back("Test2::Call::OnServerTrailingMetaData");
    }
    static inline const NoInterceptor OnFinalize;

   private:
  };

  bool StartTransportOp(grpc_transport_op* op) {
    history.push_back("Test2::StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) {
    history.push_back("Test2::GetChannelInfo");
    return false;
  }
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
    void OnServerTrailingMetadata(ServerMetadata&) {
      history.push_back("Test3::Call::OnServerTrailingMetaData");
    }
    static inline const NoInterceptor OnClientToServerHalfClose;
    void OnFinalize(const grpc_call_final_info*) {
      history.push_back("Test3::Call::OnFinalize");
    }

   private:
  };

  bool StartTransportOp(grpc_transport_op* op) {
    history.push_back("Test3:StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) {
    history.push_back("Test3::GetChannelInfo");
    return false;
  }
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
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerTrailingMetadata;
    void OnFinalize(grpc_call_final_info*, Test4*) {
      history.push_back("Test4::Call::OnFinalize");
    }

   private:
  };

  bool StartTransportOp(grpc_transport_op* op) {
    history.push_back("Test4::StartTransportOp");
    return false;
  }

  bool GetChannelInfo(const grpc_channel_info* info) {
    history.push_back("Test4::GetChannelInfo");
    return false;
  }
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
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnFinalize;

   private:
  };

  bool StartTransportOp(grpc_transport_op* op) {
    history.push_back("Test5::StartTransportOp");
    return true;
  }

  bool GetChannelInfo(const grpc_channel_info* info) {
    history.push_back("Test5::GetChannelInfo");
    return true;
  }
};

using TestFusedClient = FusedClientFilter<Test1, Test2, Test3, Test4, Test5>;

static_assert(
    !std::is_same_v<decltype(&TestFusedClient::Call::OnClientInitialMetadata),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedClient::Call::OnServerInitialMetadata),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedClient::Call::OnClientToServerMessage),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedClient::Call::OnServerToClientMessage),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedClient::Call::OnClientToServerHalfClose),
                    const NoInterceptor*>);
static_assert(
    !std::is_same_v<decltype(&TestFusedClient::Call::OnServerTrailingMetadata),
                    const NoInterceptor*>);
static_assert(!std::is_same_v<decltype(&TestFusedClient::Call::OnFinalize),
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
  TestFusedClient filter;
  TestFusedClient::Call call;
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
                                   "Test1::Call::OnServerToClientMessage",
                                   "Test2::Call::OnServerToClientMessage",
                                   "Test3::Call::OnServerToClientMessage",
                                   "Test4::Call::OnServerInitialMetadata",
                                   "Test5::Call::OnServerInitialMetadata",
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
