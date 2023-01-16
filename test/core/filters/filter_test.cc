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

#include "absl/strings/str_cat.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// FilterTest::Call

FilterTest::Call::Call(const FilterTest& test) : channel_(test.channel_) {}

ClientMetadataHandle FilterTest::Call::NewClientMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = arena_->MakePooled<ClientMetadata>(arena_.get());
  for (auto& p : init) {
    auto parsed = ClientMetadata::Parse(
        p.first, Slice::FromCopiedString(p.second),
        p.first.length() + p.second.length() + 32,
        [p](absl::string_view, const Slice&) {
          Crash(absl::StrCat("Illegal metadata value: ", p.first, ": ",
                             p.second));
        });
    md->Set(parsed);
  }
  return md;
}

ServerMetadataHandle FilterTest::Call::NewServerMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = arena_->MakePooled<ClientMetadata>(arena_.get());
  for (auto& p : init) {
    auto parsed = ServerMetadata::Parse(
        p.first, Slice::FromCopiedString(p.second),
        p.first.length() + p.second.length() + 32,
        [p](absl::string_view, const Slice&) {
          Crash(absl::StrCat("Illegal metadata value: ", p.first, ": ",
                             p.second));
        });
    md->Set(parsed);
  }
  return md;
}

void FilterTest::Call::Start(ClientMetadataHandle md) {
  EXPECT_EQ(promise_, absl::nullopt);
  ScopedContext ctx(this);
  promise_ = channel_->filter->MakeCallPromise(
      CallArgs{std::move(md), nullptr, nullptr, nullptr},
      [this](CallArgs args) -> ArenaPromise<ServerMetadataHandle> {
        Started(*args.client_initial_metadata);
        return [this]() { return PollNextFilter(); };
      });
  EXPECT_NE(promise_, absl::nullopt);
}

void FilterTest::Call::FinishNextFilter(ServerMetadataHandle md) {
  poll_next_filter_result_ = std::move(md);
}

Poll<ServerMetadataHandle> FilterTest::Call::PollNextFilter() {
  return std::exchange(poll_next_filter_result_, Pending());
}

void FilterTest::Call::Step() {
  EXPECT_NE(promise_, absl::nullopt);
  ScopedContext ctx(this);
  for (;;) {
    auto r = (*promise_)();
    if (absl::holds_alternative<Pending>(r)) return;
    promise_.reset();
    Finished(*absl::get<ServerMetadataHandle>(r));
    return;
  }
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest

}  // namespace grpc_core
