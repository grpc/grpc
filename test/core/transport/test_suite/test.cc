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

#include "test/core/transport/test_suite/test.h"

#include <initializer_list>

#include "absl/random/random.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// TransportTestRegistry

TransportTestRegistry& TransportTestRegistry::Get() {
  static TransportTestRegistry* registry = new TransportTestRegistry();
  return *registry;
}

void TransportTestRegistry::RegisterTest(
    absl::string_view name,
    absl::AnyInvocable<TransportTest*(std::unique_ptr<TransportFixture>,
                                      const fuzzing_event_engine::Actions&,
                                      absl::BitGenRef) const>
        create) {
  if (absl::StartsWith(name, "DISABLED_")) return;
  tests_.push_back({name, std::move(create)});
}

///////////////////////////////////////////////////////////////////////////////
// TransportTest

void TransportTest::RunTest() {
  TestImpl();
  EXPECT_EQ(pending_actions_.size(), 0)
      << "There are still pending actions: did you forget to call "
         "WaitForAllPendingWork()?";
  transport_pair_.client.reset();
  transport_pair_.server.reset();
  event_engine_->TickUntilIdle();
  event_engine_->UnsetGlobalHooks();
}

void TransportTest::SetServerAcceptor() {
  transport_pair_.server->server_transport()->SetAcceptor(&acceptor_);
}

CallInitiator TransportTest::CreateCall() {
  auto call = MakeCall(event_engine_.get(), Arena::Create(1024, &allocator_));
  call.handler.SpawnInfallible("start-call", [this, handler = call.handler]() {
    transport_pair_.client->client_transport()->StartCall(handler);
    return Empty{};
  });
  return std::move(call.initiator);
}

CallHandler TransportTest::TickUntilServerCall() {
  WatchDog watchdog(this);
  for (;;) {
    auto handler = acceptor_.PopHandler();
    if (handler.has_value()) return std::move(*handler);
    event_engine_->Tick();
  }
}

void TransportTest::WaitForAllPendingWork() {
  WatchDog watchdog(this);
  while (!pending_actions_.empty()) {
    if (pending_actions_.front()->IsDone()) {
      pending_actions_.pop();
      continue;
    }
    event_engine_->Tick();
  }
}

void TransportTest::Timeout() {
  std::vector<std::string> lines;
  lines.emplace_back("Timeout waiting for pending actions to complete");
  while (!pending_actions_.empty()) {
    auto action = std::move(pending_actions_.front());
    pending_actions_.pop();
    if (action->IsDone()) continue;
    absl::string_view state_name =
        transport_test_detail::ActionState::StateString(action->Get());
    absl::string_view file_name = action->file();
    auto pos = file_name.find_last_of('/');
    if (pos != absl::string_view::npos) {
      file_name = file_name.substr(pos + 1);
    }
    lines.emplace_back(absl::StrCat("  ", state_name, " ", action->name(), " [",
                                    action->step(), "]: ", file_name, ":",
                                    action->line()));
  }
  Crash(absl::StrJoin(lines, "\n"));
}

std::string TransportTest::RandomString(int min_length, int max_length,
                                        absl::string_view character_set) {
  std::string out;
  int length = absl::LogUniform<int>(rng_, min_length, max_length + 1);
  for (int i = 0; i < length; ++i) {
    out.push_back(
        character_set[absl::Uniform<uint8_t>(rng_, 0, character_set.size())]);
  }
  return out;
}

std::string TransportTest::RandomStringFrom(
    std::initializer_list<absl::string_view> choices) {
  size_t idx = absl::Uniform<size_t>(rng_, 0, choices.size());
  auto it = choices.begin();
  for (size_t i = 0; i < idx; ++i) ++it;
  return std::string(*it);
}

std::string TransportTest::RandomMetadataKey() {
  if (absl::Bernoulli(rng_, 0.1)) {
    return RandomStringFrom({
        ":path",
        ":method",
        ":status",
        ":authority",
        ":scheme",
    });
  }
  std::string out;
  do {
    out = RandomString(1, 128, "abcdefghijklmnopqrstuvwxyz-_");
  } while (absl::EndsWith(out, "-bin"));
  return out;
}

std::string TransportTest::RandomMetadataValue(absl::string_view key) {
  if (key == ":method") {
    return RandomStringFrom({"GET", "POST", "PUT"});
  }
  if (key == ":status") {
    return absl::StrCat(absl::Uniform<int>(rng_, 100, 600));
  }
  if (key == ":scheme") {
    return RandomStringFrom({"http", "https"});
  }
  if (key == "te") {
    return "trailers";
  }
  static const NoDestruct<std::string> kChars{[]() {
    std::string out;
    for (char c = 32; c < 127; c++) out.push_back(c);
    return out;
  }()};
  return RandomString(0, 128, *kChars);
}

std::string TransportTest::RandomMetadataBinaryKey() {
  return RandomString(1, 128, "abcdefghijklmnopqrstuvwxyz-_") + "-bin";
}

std::string TransportTest::RandomMetadataBinaryValue() {
  static const NoDestruct<std::string> kChars{[]() {
    std::string out;
    for (int c = 0; c < 256; c++) {
      out.push_back(static_cast<char>(static_cast<uint8_t>(c)));
    }
    return out;
  }()};
  return RandomString(0, 4096, *kChars);
}

std::vector<std::pair<std::string, std::string>>
TransportTest::RandomMetadata() {
  size_t size = 0;
  const size_t max_size = absl::LogUniform<size_t>(rng_, 64, 8000);
  std::vector<std::pair<std::string, std::string>> out;
  for (;;) {
    std::string key;
    std::string value;
    if (absl::Bernoulli(rng_, 0.1)) {
      key = RandomMetadataBinaryKey();
      value = RandomMetadataBinaryValue();
    } else {
      key = RandomMetadataKey();
      value = RandomMetadataValue(key);
    }
    bool include = true;
    for (size_t i = 0; i < out.size(); ++i) {
      if (out[i].first == key) {
        include = false;
        break;
      }
    }
    if (!include) continue;
    size_t this_size = 32 + key.size() + value.size();
    if (size + this_size > max_size) {
      if (out.empty()) continue;
      break;
    }
    size += this_size;
    out.emplace_back(std::move(key), std::move(value));
  }
  return out;
}

std::string TransportTest::RandomMessage() {
  static const NoDestruct<std::string> kChars{[]() {
    std::string out;
    for (int c = 0; c < 256; c++) {
      out.push_back(static_cast<char>(static_cast<uint8_t>(c)));
    }
    return out;
  }()};
  return RandomString(0, 1024 * 1024, *kChars);
}

///////////////////////////////////////////////////////////////////////////////
// TransportTest::Acceptor

Arena* TransportTest::Acceptor::CreateArena() {
  return Arena::Create(1024, allocator_);
}

absl::StatusOr<CallInitiator> TransportTest::Acceptor::CreateCall(
    ClientMetadata&, Arena* arena) {
  auto call = MakeCall(event_engine_, arena);
  handlers_.push(std::move(call.handler));
  return std::move(call.initiator);
}

absl::optional<CallHandler> TransportTest::Acceptor::PopHandler() {
  if (!handlers_.empty()) {
    auto handler = std::move(handlers_.front());
    handlers_.pop();
    return handler;
  }
  return absl::nullopt;
}

///////////////////////////////////////////////////////////////////////////////
// ActionState

namespace transport_test_detail {

ActionState::ActionState(NameAndLocation name_and_location)
    : name_and_location_(name_and_location), state_(kNotCreated) {}

bool ActionState::IsDone() {
  switch (state_) {
    case kNotCreated:
    case kNotStarted:
    case kStarted:
      return false;
    case kDone:
    case kCancelled:
      return true;
  }
}

}  // namespace transport_test_detail

}  // namespace grpc_core
