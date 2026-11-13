//
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
//

#include <google/protobuf/text_format.h>
#include <grpc/grpc.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "fuzztest/fuzztest.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/load_balancing/autosharding/autosharding_assignment.h"
#include "src/core/load_balancing/autosharding/autosharding_client.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/util/wait_for_single_owner.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/load_balancing/autosharding_client_fuzzer.pb.h"
#include "test/core/xds/xds_transport_fake.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace grpc_core {
namespace {

using ::grpc_event_engine::experimental::FuzzingEventEngine;

class FakeServerTarget final : public XdsBootstrap::XdsServerTarget {
 public:
  const std::string& server_uri() const override { return server_uri_; }
  std::string Key() const override { return server_uri_; }
  bool Equals(const XdsServerTarget& other) const override {
    return Key() == other.Key();
  }

 private:
  std::string server_uri_ = "autosharding.example.com";
};

class Fuzzer {
 public:
  explicit Fuzzer(const autosharding_client_fuzzer::Msg& msg) {
    event_engine_ = std::make_shared<FuzzingEventEngine>(
        FuzzingEventEngine::Options(), msg.fuzzing_event_engine_actions());
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { Crash("Multiple concurrent reads"); }, event_engine_);
    // Let the fuzzer decide when and how each send from the client completes.
    transport_factory_->SetAutoCompleteMessagesFromClient(false);
    transport_factory_->SetAbortOnUndrainedMessages(false);
    absl::Status status;
    transport_ = transport_factory_->GetTransport(server_target_, &status);
    GRPC_CHECK(status.ok()) << status;
    client_ = MakeOrphanable<AutoshardingClient>(
        transport_, "autosharding_target", "client_uuid",
        Duration::Milliseconds(msg.initial_assignment_timeout_ms()),
        event_engine_,
        [this](RefCountedPtr<AutoshardingClient> /*client*/,
               absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>>
                   result) { OnResult(std::move(result)); });
  }

  ~Fuzzer() {
    client_orphaned_ = true;
    client_.reset();
    transport_.reset();
    transport_factory_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  void Act(const autosharding_client_fuzzer::Action& action) {
    switch (action.action_type_case()) {
      case autosharding_client_fuzzer::Action::kResetBackoff:
        LOG(INFO) << "### ResetBackoff()";
        client_->ResetBackoff();
        break;
      case autosharding_client_fuzzer::Action::kReadMessageFromClient:
        ReadMessageFromClient(action.read_message_from_client().ok());
        break;
      case autosharding_client_fuzzer::Action::kSendMessageToClient:
        SendMessageToClient(action.send_message_to_client());
        break;
      case autosharding_client_fuzzer::Action::kSendStatusToClient:
        SendStatusToClient(
            ToAbslStatus(action.send_status_to_client().status()));
        break;
      case autosharding_client_fuzzer::Action::ACTION_TYPE_NOT_SET:
        break;
    }
  }

 private:
  using Stream = FakeXdsTransportFactory::FakeStreamingCall;

  // Invoked while holding the client's lock, so it must not call into the
  // client.
  void OnResult(
      absl::StatusOr<RefCountedPtr<const AutoshardingAssignment>> result) {
    LOG(INFO) << "==> OnResult(): "
              << (result.ok() ? "assignment" : result.status().ToString());
    GRPC_CHECK(!client_orphaned_) << "result reported after Orphan()";
    if (result.ok()) {
      GRPC_CHECK(*result != nullptr) << "null assignment reported";
      seen_assignment_ = true;
    } else {
      GRPC_CHECK(!seen_assignment_) << "error reported after an assignment";
    }
  }

  static absl::Status ToAbslStatus(
      const autosharding_client_fuzzer::Status& status) {
    return absl::Status(static_cast<absl::StatusCode>(status.code()),
                        status.message());
  }

  // Returns the client's current stream.  If there is none (for example,
  // because the retry timer is pending), ticks the EventEngine until there
  // is one or the EventEngine is idle.
  RefCountedPtr<Stream> GetStream() {
    return transport_factory_->WaitForStream(server_target_,
                                             AutoshardingClient::kMethod);
  }

  void ReadMessageFromClient(bool ok) {
    LOG(INFO) << "### ReadMessageFromClient(" << (ok ? "true" : "false") << ")";
    RefCountedPtr<Stream> stream = GetStream();
    if (stream == nullptr) return;
    std::optional<std::string> message = stream->WaitForMessageFromClient();
    if (!message.has_value()) return;
    stream->CompleteSendMessageFromClient(ok);
  }

  void SendMessageToClient(
      const autosharding_client_fuzzer::SendMessageToClient& message) {
    LOG(INFO) << "### SendMessageToClient()";
    std::string payload;
    switch (message.payload_case()) {
      case autosharding_client_fuzzer::SendMessageToClient::kResponse:
        payload = message.response().SerializeAsString();
        break;
      case autosharding_client_fuzzer::SendMessageToClient::kRawBytes:
        payload = message.raw_bytes();
        break;
      case autosharding_client_fuzzer::SendMessageToClient::PAYLOAD_NOT_SET:
        break;
    }
    RefCountedPtr<Stream> stream = GetStream();
    if (stream == nullptr) return;
    stream->SendMessageToClient(payload);
  }

  void SendStatusToClient(absl::Status status) {
    LOG(INFO) << "### SendStatusToClient(): " << status;
    RefCountedPtr<Stream> stream = GetStream();
    if (stream == nullptr) return;
    stream->MaybeSendStatusToClient(std::move(status));
  }

  std::shared_ptr<FuzzingEventEngine> event_engine_;
  FakeServerTarget server_target_;
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsTransport> transport_;
  OrphanablePtr<AutoshardingClient> client_;
  // Accessed only from the fuzzer thread: FuzzingEventEngine runs all
  // callbacks inside Tick().
  bool client_orphaned_ = false;
  bool seen_assignment_ = false;
};

// Two chunks and the metadata of a valid assignment, then the ACK.
constexpr char kValidAssignment[] = R"pb(
  initial_assignment_timeout_ms: 10000
  actions { read_message_from_client { ok: true } }
  actions {
    send_message_to_client {
      response {
        chunk {
          slice_assignments {
            slice { start_key: "" end_key: "m" }
            endpoints { endpoint_index: 0 }
          }
          endpoints { endpoint: "host0" }
        }
      }
    }
  }
  actions {
    send_message_to_client {
      response {
        chunk {
          slice_assignments {
            slice { start_key: "m" }
            endpoints { endpoint_index: 1 }
          }
          endpoints { endpoint: "host1" }
        }
      }
    }
  }
  actions { send_message_to_client { response { metadata { generation: 1 } } } }
  actions { read_message_from_client { ok: true } }
)pb";

// An invalid assignment, then a stale generation after a valid one.
constexpr char kInvalidAndStaleAssignments[] = R"pb(
  initial_assignment_timeout_ms: 10000
  actions { read_message_from_client { ok: true } }
  actions {
    send_message_to_client {
      response {
        chunk {
          slice_assignments {
            slice { start_key: "b" end_key: "a" }
            endpoints { endpoint_index: 5 }
          }
        }
      }
    }
  }
  actions { send_message_to_client { response { metadata { generation: 3 } } } }
  actions {
    send_message_to_client {
      response {
        chunk {
          slice_assignments {
            slice { start_key: "" }
            endpoints { endpoint_index: 0 }
          }
          endpoints { endpoint: "host0" }
        }
      }
    }
  }
  actions { send_message_to_client { response { metadata { generation: 2 } } } }
  actions { send_message_to_client { response { metadata { generation: 1 } } } }
  actions { read_message_from_client { ok: true } }
  actions { read_message_from_client { ok: true } }
)pb";

// Unparseable bytes, then a stream failure, and ResetBackoff.
constexpr char kStreamFailures[] = R"pb(
  initial_assignment_timeout_ms: 5000
  actions { send_message_to_client { raw_bytes: "\xff\xff\xff" } }
  actions {
    send_status_to_client { status { code: 14 message: "unavailable" } }
  }
  actions { reset_backoff {} }
  actions { read_message_from_client { ok: false } }
  actions { send_status_to_client { status { code: 13 message: "internal" } } }
)pb";

autosharding_client_fuzzer::Msg ParseTestProto(const std::string& proto) {
  autosharding_client_fuzzer::Msg msg;
  GRPC_CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  return msg;
}

void Fuzz(const autosharding_client_fuzzer::Msg& message) {
  Fuzzer fuzzer(message);
  for (const autosharding_client_fuzzer::Action& action : message.actions()) {
    fuzzer.Act(action);
  }
}
FUZZ_TEST(AutoshardingClientFuzzer, Fuzz)
    .WithDomains(::fuzztest::Arbitrary<autosharding_client_fuzzer::Msg>()
                     .WithSeeds({ParseTestProto(kValidAssignment),
                                 ParseTestProto(kInvalidAndStaleAssignments),
                                 ParseTestProto(kStreamFailures)}));

}  // namespace
}  // namespace grpc_core
