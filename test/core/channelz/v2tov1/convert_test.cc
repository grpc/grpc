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

#include "src/core/channelz/v2tov1/convert.h"

#include <google/protobuf/text_format.h>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "fuzztest/fuzztest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_reader.h"
#include "src/proto/grpc/channelz/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "src/proto/grpc/channelz/v2/property_list.pb.h"

namespace grpc_core {
namespace channelz {
namespace v2tov1 {
namespace {

int force_dependencies = []() {
  (void)grpc::channelz::v2::PropertyList::descriptor();
  return 0;
}();

using ::testing::Return;

class MockEntityFetcher : public EntityFetcher {
 public:
  MOCK_METHOD(absl::StatusOr<std::string>, GetEntity, (int64_t id), (override));
  MOCK_METHOD(absl::StatusOr<std::vector<std::string>>, GetEntitiesWithParent,
              (int64_t parent_id), (override));
};

class FakeEntityFetcher : public EntityFetcher {
 public:
  explicit FakeEntityFetcher(absl::flat_hash_map<int64_t, std::string> entities)
      : entities_(std::move(entities)) {}

  absl::StatusOr<std::string> GetEntity(int64_t id) override {
    auto it = entities_.find(id);
    if (it == entities_.end()) {
      return absl::NotFoundError("Entity not found");
    }
    return it->second;
  }

  absl::StatusOr<std::vector<std::string>> GetEntitiesWithParent(
      int64_t parent_id) override {
    std::vector<std::string> result;
    for (const auto& pair : entities_) {
      grpc::channelz::v2::Entity entity_proto;
      if (!entity_proto.ParseFromString(pair.second)) {
        continue;
      }
      for (int64_t parent : entity_proto.parents()) {
        if (parent == parent_id) {
          result.push_back(pair.second);
        }
      }
    }
    return result;
  }

 private:
  absl::flat_hash_map<int64_t, std::string> entities_;
};

auto ParseEntity(const std::string& proto) {
  grpc::channelz::v2::Entity msg;
  CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  return msg.SerializeAsString();
}

TEST(ConvertTest, ServerBasic) {
  const auto v2 = ParseEntity(R"pb(
    id: 3
    kind: "server"
    data {
      name: "call_counts"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "calls_failed"
            value { int64_value: 0 }
          }
          properties {
            key: "calls_started"
            value { int64_value: 4825 }
          }
          properties {
            key: "calls_succeeded"
            value { int64_value: 4823 }
          }
          properties {
            key: "last_call_started_timestamp"
            value { timestamp_value { seconds: 1751558004 nanos: 820386861 } }
          }
        }
      }
    }
    trace {
      description: "Server created"
      timestamp { seconds: 1751557720 nanos: 556388143 }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertServer(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Server v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().server_id(), 3);
  EXPECT_EQ(v1.data().calls_started(), 4825);
  EXPECT_EQ(v1.data().calls_succeeded(), 4823);
  EXPECT_EQ(v1.data().calls_failed(), 0);
  EXPECT_EQ(v1.data().last_call_started_timestamp().seconds(), 1751558004);
  EXPECT_EQ(v1.data().last_call_started_timestamp().nanos(), 820386861);
  ASSERT_EQ(v1.data().trace().events().size(), 1);
  EXPECT_EQ(v1.data().trace().events(0).description(), "Server created");
  EXPECT_EQ(v1.data().trace().events(0).timestamp().seconds(), 1751557720);
  EXPECT_EQ(v1.data().trace().events(0).timestamp().nanos(), 556388143);
  EXPECT_EQ(v1.data().trace().events(0).severity(),
            grpc::channelz::v1::ChannelTraceEvent::CT_INFO);
}

TEST(ConvertTest, ServerBasicJson) {
  const auto v2 = ParseEntity(R"pb(
    id: 3
    kind: "server"
    data {
      name: "call_counts"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "calls_failed"
            value { int64_value: 0 }
          }
          properties {
            key: "calls_started"
            value { int64_value: 4825 }
          }
          properties {
            key: "calls_succeeded"
            value { int64_value: 4823 }
          }
          properties {
            key: "last_call_started_timestamp"
            value { timestamp_value { seconds: 1751558004 nanos: 820386861 } }
          }
        }
      }
    }
    trace {
      description: "Server created"
      timestamp { seconds: 1751557720 nanos: 556388143 }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertServer(v2, fetcher, true);
  ASSERT_TRUE(v1_str.ok());
  auto v1_json = JsonParse(*v1_str);
  ASSERT_TRUE(v1_json.ok());
  EXPECT_EQ(v1_json->type(), Json::Type::kObject);
  const auto& obj = v1_json->object();
  auto it = obj.find("ref");
  ASSERT_NE(it, obj.end());
  EXPECT_EQ(it->second.object().at("serverId").string(), "3");
  it = obj.find("data");
  ASSERT_NE(it, obj.end());
  const auto& data = it->second.object();
  EXPECT_EQ(data.at("callsStarted").string(), "4825");
  EXPECT_EQ(data.at("callsSucceeded").string(), "4823");
  EXPECT_EQ(data.at("callsFailed").string(), "0");
  auto trace_it = data.find("trace");
  ASSERT_NE(trace_it, data.end());
  const auto& trace = trace_it->second.object();
  auto events_it = trace.find("events");
  ASSERT_NE(events_it, trace.end());
  const auto& events = events_it->second.array();
  ASSERT_EQ(events.size(), 1);
  const auto& event = events[0].object();
  EXPECT_EQ(event.at("description").string(), "Server created");
  EXPECT_EQ(event.at("severity").string(), "CT_INFO");
}

void FuzzConvertServer(
    const grpc::channelz::v2::Entity& entity_proto,
    const absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity>&
        children_proto,
    bool json) {
  absl::flat_hash_map<int64_t, std::string> children;
  for (const auto& pair : children_proto) {
    children[pair.first] = pair.second.SerializeAsString();
  }
  FakeEntityFetcher fetcher(std::move(children));
  (void)ConvertServer(entity_proto.SerializeAsString(), fetcher, json);
}
FUZZ_TEST(ConvertTest, FuzzConvertServer);

TEST(ConvertTest, ServerWrongKind) {
  const auto v2 = ParseEntity(R"pb(
    id: 1 kind: "socket"
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1 = ConvertServer(v2, fetcher, false);
  EXPECT_FALSE(v1.ok());
  EXPECT_EQ(v1.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ConvertTest, SocketBasic) {
  const auto v2 = ParseEntity(R"pb(
    id: 1
    kind: "socket"
    data {
      name: "v1_compatibility"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "name"
            value { string_value: "test-socket-name" }
          }
        }
      }
    }
    data {
      name: "call_counts"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "streams_started"
            value { int64_value: 2291 }
          }
          properties {
            key: "streams_succeeded"
            value { int64_value: 2290 }
          }
          properties {
            key: "streams_failed"
            value { int64_value: 0 }
          }
          properties {
            key: "messages_sent"
            value { int64_value: 2290 }
          }
          properties {
            key: "messages_received"
            value { int64_value: 2291 }
          }
          properties {
            key: "keepalives_sent"
            value { int64_value: 76 }
          }
          properties {
            key: "last_remote_stream_created_timestamp"
            value { timestamp_value { seconds: 1751557952, nanos: 648387472 } }
          }
          properties {
            key: "last_local_stream_created_timestamp"
            value { timestamp_value { seconds: 1, nanos: 1 } }
          }
          properties {
            key: "last_message_sent_timestamp"
            value { timestamp_value { seconds: 1751557952, nanos: 649388292 } }
          }
          properties {
            key: "last_message_received_timestamp"
            value { timestamp_value { seconds: 1751557952, nanos: 649388260 } }
          }
        }
      }
    }
    data {
      name: "http2"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "flow_control"
            value {
              any_value {
                [type.googleapis.com/grpc.channelz.v2.PropertyList] {
                  properties {
                    key: "remote_window"
                    value { int64_value: 4194291 }
                  }
                  properties {
                    key: "announced_window"
                    value { int64_value: 4194304 }
                  }
                }
              }
            }
          }
        }
      }
    }
    data {
      name: "socket"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "local"
            value { string_value: "ipv4:127.0.0.1:10000" }
          }
          properties {
            key: "remote"
            value { string_value: "ipv4:127.0.0.1:32900" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSocket(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Socket v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().socket_id(), 1);
  EXPECT_EQ(v1.ref().name(), "test-socket-name");
  EXPECT_EQ(v1.data().streams_started(), 2291);
  EXPECT_EQ(v1.data().streams_succeeded(), 2290);
  EXPECT_EQ(v1.data().streams_failed(), 0);
  EXPECT_EQ(v1.data().messages_sent(), 2290);
  EXPECT_EQ(v1.data().messages_received(), 2291);
  EXPECT_EQ(v1.data().keep_alives_sent(), 76);
  EXPECT_EQ(v1.data().last_remote_stream_created_timestamp().seconds(),
            1751557952);
  EXPECT_EQ(v1.data().last_remote_stream_created_timestamp().nanos(),
            648387472);
  EXPECT_EQ(v1.data().last_local_stream_created_timestamp().seconds(), 1);
  EXPECT_EQ(v1.data().last_local_stream_created_timestamp().nanos(), 1);
  EXPECT_EQ(v1.data().last_message_sent_timestamp().seconds(), 1751557952);
  EXPECT_EQ(v1.data().last_message_sent_timestamp().nanos(), 649388292);
  EXPECT_EQ(v1.data().last_message_received_timestamp().seconds(), 1751557952);
  EXPECT_EQ(v1.data().last_message_received_timestamp().nanos(), 649388260);
  EXPECT_EQ(v1.data().local_flow_control_window().value(), 4194291);
  EXPECT_EQ(v1.data().remote_flow_control_window().value(), 4194304);
  EXPECT_EQ(v1.local().tcpip_address().port(), 10000);
  EXPECT_EQ(v1.remote().tcpip_address().port(), 32900);
}

TEST(ConvertTest, SocketBasicJson) {
  const auto v2 = ParseEntity(R"pb(
    id: 1
    kind: "socket"
    data {
      name: "v1_compatibility"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "name"
            value { string_value: "test-socket-name" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSocket(v2, fetcher, true);
  ASSERT_TRUE(v1_str.ok());
  auto v1_json = JsonParse(*v1_str);
  ASSERT_TRUE(v1_json.ok());
  EXPECT_EQ(v1_json->type(), Json::Type::kObject);
  const auto& obj = v1_json->object();
  auto it = obj.find("ref");
  ASSERT_NE(it, obj.end());
  const auto& ref = it->second.object();
  EXPECT_EQ(ref.at("socketId").string(), "1");
  EXPECT_EQ(ref.at("name").string(), "test-socket-name");
}

void FuzzConvertSocket(const grpc::channelz::v2::Entity& entity_proto,
                       bool json) {
  FakeEntityFetcher fetcher({});
  (void)ConvertSocket(entity_proto.SerializeAsString(), fetcher, json);
}
FUZZ_TEST(ConvertTest, FuzzConvertSocket);

TEST(ConvertTest, SocketWrongKind) {
  const auto v2 = ParseEntity(R"pb(
    id: 1 kind: "server"
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1 = ConvertSocket(v2, fetcher, false);
  EXPECT_FALSE(v1.ok());
  EXPECT_EQ(v1.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ConvertTest, SocketWithSecurity) {
  const auto v2 = ParseEntity(R"pb(
    id: 1
    kind: "socket"
    data {
      name: "security"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "standard_name"
            value { string_value: "tls" }
          }
          properties {
            key: "local_certificate"
            value { string_value: "Zm9v" }  # "foo"
          }
          properties {
            key: "remote_certificate"
            value { string_value: "YmFy" }  # "bar"
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSocket(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Socket v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().socket_id(), 1);
  EXPECT_EQ(v1.security().tls().standard_name(), "tls");
  EXPECT_EQ(v1.security().tls().local_certificate(), "foo");
  EXPECT_EQ(v1.security().tls().remote_certificate(), "bar");
}

TEST(ConvertTest, SocketWithIpv6Address) {
  const auto v2 = ParseEntity(R"pb(
    id: 1
    kind: "socket"
    data {
      name: "socket"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "remote"
            value { string_value: "ipv6:[::1]:12345" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSocket(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Socket v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.remote().tcpip_address().port(), 12345);
  EXPECT_EQ(v1.remote().tcpip_address().ip_address().size(), 16);
}

TEST(ConvertTest, SocketWithUdsAddress) {
  const auto v2 = ParseEntity(R"pb(
    id: 1
    kind: "socket"
    data {
      name: "socket"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "remote"
            value { string_value: "unix:/tmp/foo.sock" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSocket(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Socket v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.remote().uds_address().filename(), "/tmp/foo.sock");
}

TEST(ConvertTest, SocketWithOtherAddress) {
  const auto v2 = ParseEntity(R"pb(
    id: 1
    kind: "socket"
    data {
      name: "socket"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "remote"
            value { string_value: "some-other-address" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSocket(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Socket v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.remote().other_address().name(), "some-other-address");
}

TEST(ConvertTest, ChannelBasic) {
  const auto v2 = ParseEntity(R"pb(
    id: 4
    kind: "channel"
    data {
      name: "channel"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "target"
            value { string_value: "some-target" }
          }
          properties {
            key: "connectivity_state"
            value { string_value: "READY" }
          }
        }
      }
    }
    data {
      name: "call_counts"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "calls_started"
            value { int64_value: 1 }
          }
          properties {
            key: "calls_succeeded"
            value { int64_value: 2 }
          }
          properties {
            key: "calls_failed"
            value { int64_value: 3 }
          }
          properties {
            key: "last_call_started_timestamp"
            value { timestamp_value { seconds: 4, nanos: 5 } }
          }
        }
      }
    }
    trace {
      description: "Channel created"
      timestamp { seconds: 123, nanos: 456 }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertChannel(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Channel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().channel_id(), 4);
  EXPECT_EQ(v1.ref().name(), "");
  EXPECT_EQ(v1.data().state().state(),
            grpc::channelz::v1::ChannelConnectivityState::READY);
  EXPECT_EQ(v1.data().target(), "some-target");
  EXPECT_EQ(v1.data().calls_started(), 1);
  EXPECT_EQ(v1.data().calls_succeeded(), 2);
  EXPECT_EQ(v1.data().calls_failed(), 3);
  EXPECT_EQ(v1.data().last_call_started_timestamp().seconds(), 4);
  EXPECT_EQ(v1.data().last_call_started_timestamp().nanos(), 5);
  ASSERT_EQ(v1.data().trace().events().size(), 1);
  EXPECT_EQ(v1.data().trace().events(0).description(), "Channel created");
  EXPECT_EQ(v1.data().trace().events(0).timestamp().seconds(), 123);
  EXPECT_EQ(v1.data().trace().events(0).timestamp().nanos(), 456);
}

TEST(ConvertTest, ChannelBasicJson) {
  const auto v2 = ParseEntity(R"pb(
    id: 4
    kind: "channel"
    data {
      name: "channel"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "target"
            value { string_value: "some-target" }
          }
          properties {
            key: "connectivity_state"
            value { string_value: "READY" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertChannel(v2, fetcher, true);
  ASSERT_TRUE(v1_str.ok());
  auto v1_json = JsonParse(*v1_str);
  ASSERT_TRUE(v1_json.ok());
  EXPECT_EQ(v1_json->type(), Json::Type::kObject);
  const auto& obj = v1_json->object();
  auto it = obj.find("ref");
  ASSERT_NE(it, obj.end());
  const auto& ref = it->second.object();
  EXPECT_EQ(ref.at("channelId").string(), "4");
  it = obj.find("data");
  ASSERT_NE(it, obj.end());
  const auto& data = it->second.object();
  EXPECT_EQ(data.at("target").string(), "some-target");
  auto state_it = data.find("state");
  ASSERT_NE(state_it, data.end());
  const auto& state = state_it->second.object();
  EXPECT_EQ(state.at("state").string(), "READY");
}

void FuzzConvertChannel(
    const grpc::channelz::v2::Entity& entity_proto,
    const absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity>&
        children_proto,
    bool json) {
  absl::flat_hash_map<int64_t, std::string> children;
  for (const auto& pair : children_proto) {
    children[pair.first] = pair.second.SerializeAsString();
  }
  FakeEntityFetcher fetcher(std::move(children));
  (void)ConvertChannel(entity_proto.SerializeAsString(), fetcher, json);
}
FUZZ_TEST(ConvertTest, FuzzConvertChannel);

TEST(ConvertTest, ChannelWrongKind) {
  const auto v2 = ParseEntity(R"pb(
    id: 1 kind: "server"
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1 = ConvertChannel(v2, fetcher, false);
  EXPECT_FALSE(v1.ok());
  EXPECT_EQ(v1.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ConvertTest, SubchannelBasic) {
  const auto v2 = ParseEntity(R"pb(
    id: 5
    kind: "subchannel"
    data {
      name: "channel"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "target"
            value { string_value: "some-target" }
          }
          properties {
            key: "connectivity_state"
            value { string_value: "IDLE" }
          }
        }
      }
    }
    data {
      name: "call_counts"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "calls_started"
            value { int64_value: 1 }
          }
          properties {
            key: "calls_succeeded"
            value { int64_value: 2 }
          }
          properties {
            key: "calls_failed"
            value { int64_value: 3 }
          }
          properties {
            key: "last_call_started_timestamp"
            value { timestamp_value { seconds: 4, nanos: 5 } }
          }
        }
      }
    }
    trace {
      description: "Subchannel created"
      timestamp { seconds: 123, nanos: 456 }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSubchannel(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Subchannel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().subchannel_id(), 5);
  EXPECT_EQ(v1.ref().name(), "");
  EXPECT_EQ(v1.data().state().state(),
            grpc::channelz::v1::ChannelConnectivityState::IDLE);
  EXPECT_EQ(v1.data().target(), "some-target");
  EXPECT_EQ(v1.data().calls_started(), 1);
  EXPECT_EQ(v1.data().calls_succeeded(), 2);
  EXPECT_EQ(v1.data().calls_failed(), 3);
  EXPECT_EQ(v1.data().last_call_started_timestamp().seconds(), 4);
  EXPECT_EQ(v1.data().last_call_started_timestamp().nanos(), 5);
  ASSERT_EQ(v1.data().trace().events().size(), 1);
  EXPECT_EQ(v1.data().trace().events(0).description(), "Subchannel created");
  EXPECT_EQ(v1.data().trace().events(0).timestamp().seconds(), 123);
  EXPECT_EQ(v1.data().trace().events(0).timestamp().nanos(), 456);
}

TEST(ConvertTest, SubchannelBasicJson) {
  const auto v2 = ParseEntity(R"pb(
    id: 5
    kind: "subchannel"
    data {
      name: "channel"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "target"
            value { string_value: "some-target" }
          }
          properties {
            key: "connectivity_state"
            value { string_value: "IDLE" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertSubchannel(v2, fetcher, true);
  ASSERT_TRUE(v1_str.ok());
  auto v1_json = JsonParse(*v1_str);
  ASSERT_TRUE(v1_json.ok());
  EXPECT_EQ(v1_json->type(), Json::Type::kObject);
  const auto& obj = v1_json->object();
  auto it = obj.find("ref");
  ASSERT_NE(it, obj.end());
  const auto& ref = it->second.object();
  EXPECT_EQ(ref.at("subchannelId").string(), "5");
  it = obj.find("data");
  ASSERT_NE(it, obj.end());
  const auto& data = it->second.object();
  EXPECT_EQ(data.at("target").string(), "some-target");
  auto state_it = data.find("state");
  ASSERT_NE(state_it, data.end());
  const auto& state = state_it->second.object();
  EXPECT_EQ(state.at("state").string(), "IDLE");
}

void FuzzConvertSubchannel(
    const grpc::channelz::v2::Entity& entity_proto,
    const absl::flat_hash_map<int64_t, grpc::channelz::v2::Entity>&
        children_proto,
    bool json) {
  absl::flat_hash_map<int64_t, std::string> children;
  for (const auto& pair : children_proto) {
    children[pair.first] = pair.second.SerializeAsString();
  }
  FakeEntityFetcher fetcher(std::move(children));
  (void)ConvertSubchannel(entity_proto.SerializeAsString(), fetcher, json);
}
FUZZ_TEST(ConvertTest, FuzzConvertSubchannel);

TEST(ConvertTest, SubchannelWrongKind) {
  const auto v2 = ParseEntity(R"pb(
    id: 1 kind: "server"
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1 = ConvertSubchannel(v2, fetcher, false);
  EXPECT_FALSE(v1.ok());
  EXPECT_EQ(v1.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ConvertTest, ListenSocket) {
  const auto v2 = ParseEntity(R"pb(
    id: 6
    kind: "listen_socket"
    data {
      name: "v1_compatibility"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "name"
            value { string_value: "test-listen-socket-name" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertListenSocket(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Socket v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().socket_id(), 6);
  EXPECT_EQ(v1.ref().name(), "test-listen-socket-name");
}

TEST(ConvertTest, ListenSocketJson) {
  const auto v2 = ParseEntity(R"pb(
    id: 6
    kind: "listen_socket"
    data {
      name: "v1_compatibility"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "name"
            value { string_value: "test-listen-socket-name" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1_str = ConvertListenSocket(v2, fetcher, true);
  ASSERT_TRUE(v1_str.ok());
  EXPECT_EQ(
      *v1_str,
      "{\"ref\":{\"socketId\":\"6\",\"name\":\"test-listen-socket-name\"},"
      "\"remoteName\":\"\"}");
}

void FuzzConvertListenSocket(const grpc::channelz::v2::Entity& entity_proto,
                             bool json) {
  FakeEntityFetcher fetcher({});
  (void)ConvertListenSocket(entity_proto.SerializeAsString(), fetcher, json);
}
FUZZ_TEST(ConvertTest, FuzzConvertListenSocket);

TEST(ConvertTest, ListenSocketWrongKind) {
  const auto v2 = ParseEntity(R"pb(
    id: 1 kind: "server"
  )pb");
  FakeEntityFetcher fetcher({});
  auto v1 = ConvertListenSocket(v2, fetcher, false);
  EXPECT_FALSE(v1.ok());
  EXPECT_EQ(v1.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ConvertTest, ServerWithListenSocket) {
  const auto v2_server = ParseEntity(R"pb(
    id: 3 kind: "server"
  )pb");
  const auto v2_socket = ParseEntity(R"pb(
    id: 4
    kind: "listen_socket"
    parents: 3
    data {
      name: "v1_compatibility"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "name"
            value { string_value: "listener" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({{4, v2_socket}});
  auto v1_str = ConvertServer(v2_server, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Server v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().server_id(), 3);
  ASSERT_EQ(v1.listen_socket().size(), 1);
  EXPECT_EQ(v1.listen_socket(0).socket_id(), 4);
  EXPECT_EQ(v1.listen_socket(0).name(), "listener");
}

TEST(ConvertTest, ServerWithFetcherError) {
  const auto v2 = ParseEntity(R"pb(
    id: 3 kind: "server"
  )pb");
  MockEntityFetcher fetcher;
  EXPECT_CALL(fetcher, GetEntitiesWithParent(3))
      .WillOnce(Return(absl::InternalError("fetch failed")));
  auto v1_str = ConvertServer(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Server v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.listen_socket().size(), 0);
}

TEST(ConvertTest, ServerWithInvalidChild) {
  const auto v2_server = ParseEntity(R"pb(
    id: 3 kind: "server"
  )pb");
  const auto v2_invalid_child = ParseEntity(R"pb(
    id: 4 kind: "socket" parents: 3
  )pb");
  FakeEntityFetcher fetcher({{4, v2_invalid_child}});
  auto v1_str = ConvertServer(v2_server, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Server v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.listen_socket().size(), 0);
}

TEST(ConvertTest, ChannelWithChildren) {
  const auto v2_channel = ParseEntity(R"pb(
    id: 10 kind: "channel"
  )pb");
  const auto v2_child_channel =
      ParseEntity(R"pb(
        id: 11 kind: "channel" parents: 10
      )pb");
  const auto v2_subchannel =
      ParseEntity(R"pb(
        id: 12 kind: "subchannel" parents: 10
      )pb");
  FakeEntityFetcher fetcher({{11, v2_child_channel}, {12, v2_subchannel}});
  auto v1_str = ConvertChannel(v2_channel, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Channel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().channel_id(), 10);
  ASSERT_EQ(v1.channel_ref().size(), 1);
  EXPECT_EQ(v1.channel_ref(0).channel_id(), 11);
  ASSERT_EQ(v1.subchannel_ref().size(), 1);
  EXPECT_EQ(v1.subchannel_ref(0).subchannel_id(), 12);
}

TEST(ConvertTest, ChannelWithFetcherError) {
  const auto v2 = ParseEntity(R"pb(
    id: 10 kind: "channel"
  )pb");
  MockEntityFetcher fetcher;
  EXPECT_CALL(fetcher, GetEntitiesWithParent(10))
      .WillOnce(Return(absl::InternalError("fetch failed")));
  auto v1_str = ConvertChannel(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Channel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.channel_ref().size(), 0);
  EXPECT_EQ(v1.subchannel_ref().size(), 0);
}

TEST(ConvertTest, ChannelWithInvalidChild) {
  const auto v2_channel = ParseEntity(R"pb(
    id: 10 kind: "channel"
  )pb");
  const auto v2_invalid_child =
      ParseEntity(R"pb(
        id: 11 kind: "listen_socket" parents: 10
      )pb");
  FakeEntityFetcher fetcher({{11, v2_invalid_child}});
  auto v1_str = ConvertChannel(v2_channel, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Channel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.channel_ref().size(), 0);
  EXPECT_EQ(v1.subchannel_ref().size(), 0);
}

TEST(ConvertTest, SubchannelWithSocket) {
  const auto v2_subchannel = ParseEntity(R"pb(
    id: 20 kind: "subchannel"
  )pb");
  const auto v2_socket = ParseEntity(R"pb(
    id: 21
    kind: "socket"
    parents: 20
    data {
      name: "v1_compatibility"
      value {
        [type.googleapis.com/grpc.channelz.v2.PropertyList] {
          properties {
            key: "name"
            value { string_value: "child-socket" }
          }
        }
      }
    }
  )pb");
  FakeEntityFetcher fetcher({{21, v2_socket}});
  auto v1_str = ConvertSubchannel(v2_subchannel, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Subchannel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().subchannel_id(), 20);
  ASSERT_EQ(v1.socket_ref().size(), 1);
  EXPECT_EQ(v1.socket_ref(0).socket_id(), 21);
  EXPECT_EQ(v1.socket_ref(0).name(), "child-socket");
}

TEST(ConvertTest, SubchannelWithChildren) {
  const auto v2_subchannel = ParseEntity(R"pb(
    id: 30 kind: "subchannel"
  )pb");
  const auto v2_child_channel =
      ParseEntity(R"pb(
        id: 31 kind: "channel" parents: 30
      )pb");
  const auto v2_child_subchannel =
      ParseEntity(R"pb(
        id: 32 kind: "subchannel" parents: 30
      )pb");
  FakeEntityFetcher fetcher(
      {{31, v2_child_channel}, {32, v2_child_subchannel}});
  auto v1_str = ConvertSubchannel(v2_subchannel, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Subchannel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.ref().subchannel_id(), 30);
  ASSERT_EQ(v1.channel_ref().size(), 1);
  EXPECT_EQ(v1.channel_ref(0).channel_id(), 31);
  ASSERT_EQ(v1.subchannel_ref().size(), 1);
  EXPECT_EQ(v1.subchannel_ref(0).subchannel_id(), 32);
}

TEST(ConvertTest, SubchannelWithFetcherError) {
  const auto v2 = ParseEntity(R"pb(
    id: 20 kind: "subchannel"
  )pb");
  MockEntityFetcher fetcher;
  EXPECT_CALL(fetcher, GetEntitiesWithParent(20))
      .WillOnce(Return(absl::InternalError("fetch failed")));
  auto v1_str = ConvertSubchannel(v2, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Subchannel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.socket_ref().size(), 0);
  EXPECT_EQ(v1.channel_ref().size(), 0);
  EXPECT_EQ(v1.subchannel_ref().size(), 0);
}

TEST(ConvertTest, SubchannelWithInvalidChild) {
  const auto v2_subchannel = ParseEntity(R"pb(
    id: 20 kind: "subchannel"
  )pb");
  const auto v2_invalid_child =
      ParseEntity(R"pb(
        id: 21 kind: "server" parents: 20
      )pb");
  FakeEntityFetcher fetcher({{21, v2_invalid_child}});
  auto v1_str = ConvertSubchannel(v2_subchannel, fetcher, false);
  ASSERT_TRUE(v1_str.ok());
  grpc::channelz::v1::Subchannel v1;
  ASSERT_TRUE(v1.ParseFromString(*v1_str));
  EXPECT_EQ(v1.socket_ref().size(), 0);
  EXPECT_EQ(v1.channel_ref().size(), 0);
  EXPECT_EQ(v1.subchannel_ref().size(), 0);
}

}  // namespace
}  // namespace v2tov1
}  // namespace channelz
}  // namespace grpc_core
