//
//
// Copyright 2015 gRPC authors.
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
//

#include <stdio.h>

#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {
const NoDestruct<std::vector<std::pair<absl::string_view, absl::string_view>>>
    hobbits(std::vector<std::pair<absl::string_view, absl::string_view>>{
        {"Adaldrida", "Brandybuck"}, {"Adamanta", "Took"},
        {"Adalgrim", "Took"},        {"Adelard", "Took"},
        {"Amaranth", "Brandybuck"},  {"Andwise", "Roper"},
        {"Angelica", "Baggins"},     {"Asphodel", "Burrows"},
        {"Balbo", "Baggins"},        {"Bandobras", "Took"},
        {"Belba", "Bolger"},         {"Bell", "Gamgee"},
        {"Belladonna", "Baggins"},   {"Berylla", "Baggins"},
        {"Bilbo", "Baggins"},        {"Bilbo", "Gardner"},
        {"Bill", "Butcher"},         {"Bingo", "Baggins"},
        {"Bodo", "Proudfoot"},       {"Bowman", "Cotton"},
        {"Bungo", "Baggins"},        {"Camellia", "Sackville"},
        {"Carl", "Cotton"},          {"Celandine", "Brandybuck"},
        {"Chica", "Baggins"},        {"Daddy", "Twofoot"},
        {"Daisy", "Boffin"},         {"Diamond", "Took"},
        {"Dinodas", "Brandybuck"},   {"Doderic", "Brandybuck"},
        {"Dodinas", "Brandybuck"},   {"Donnamira", "Boffin"},
        {"Dora", "Baggins"},         {"Drogo", "Baggins"},
        {"Dudo", "Baggins"},         {"Eglantine", "Took"},
        {"Elanor", "Fairbairn"},     {"Elfstan", "Fairbairn"},
        {"Esmeralda", "Brandybuck"}, {"Estella", "Brandybuck"},
        {"Everard", "Took"},         {"Falco", "Chubb-Baggins"},
        {"Faramir", "Took"},         {"Farmer", "Maggot"},
        {"Fastolph", "Bolger"},      {"Ferdibrand", "Took"},
        {"Ferdinand", "Took"},       {"Ferumbras", "Took"},
        {"Ferumbras", "Took"},       {"Filibert", "Bolger"},
        {"Firiel", "Fairbairn"},     {"Flambard", "Took"},
        {"Folco", "Boffin"},         {"Fortinbras", "Took"},
        {"Fortinbras", "Took"},      {"Fosco", "Baggins"},
        {"Fredegar", "Bolger"},      {"Frodo", "Baggins"},
        {"Frodo", "Gardner"},        {"Gerontius", "Took"},
        {"Gilly", "Baggins"},        {"Goldilocks", "Took"},
        {"Gorbadoc", "Brandybuck"},  {"Gorbulas", "Brandybuck"},
        {"Gorhendad", "Brandybuck"}, {"Gormadoc", "Brandybuck"},
        {"Griffo", "Boffin"},        {"Halfast", "Gamgee"},
        {"Halfred", "Gamgee"},       {"Halfred", "Greenhand"},
        {"Hanna", "Brandybuck"},     {"Hamfast", "Gamgee"},
        {"Hamfast", "Gardner"},      {"Hamson", "Gamgee"},
        {"Harding", "Gardner"},      {"Hilda", "Brandybuck"},
        {"Hildibrand", "Took"},      {"Hildifons", "Took"},
        {"Hildigard", "Took"},       {"Hildigrim", "Took"},
        {"Hob", "Gammidge"},         {"Hob", "Hayward"},
        {"Hobson", "Gamgee"},        {"Holfast", "Gardner"},
        {"Holman", "Cotton"},        {"Holman", "Greenhand"},
        {"Hugo", "Boffin"},          {"Hugo", "Bracegirdle"},
        {"Ilberic", "Brandybuck"},   {"Isembard", "Took"},
        {"Isembold", "Took"},        {"Isengar", "Took"},
        {"Isengrim", "Took"},        {"Isengrim", "Took"},
        {"Isumbras", "Took"},        {"Isumbras", "Took"},
        {"Jolly", "Cotton"},
    });

const NoDestruct<std::vector<absl::string_view>> dragons(
    std::vector<absl::string_view>{"Ancalagon", "Glaurung", "Scatha",
                                   "Smaug the Magnificent"});

void SimpleRequestBody(CoreEnd2endTest& test, size_t index) {
  const auto& hobbit = (*hobbits)[index % hobbits->size()];
  const auto& dragon = (*dragons)[index % dragons->size()];
  auto method =
      absl::StrCat("/", hobbit.first, ".", hobbit.second, "/", dragon);
  auto c = test.NewClientCall(method).Create();
  CoreEnd2endTest::IncomingStatusOnClient server_status;
  CoreEnd2endTest::IncomingMetadata server_initial_metadata;
  c.NewBatch(1)
      .SendInitialMetadata({
          {"hobbit-first-name", (*hobbits)[index % hobbits->size()].first},
          {"hobbit-second-name", (*hobbits)[index % hobbits->size()].second},
          {"dragon", (*dragons)[index % dragons->size()]},
      })
      .SendCloseFromClient()
      .RecvInitialMetadata(server_initial_metadata)
      .RecvStatusOnClient(server_status);
  auto s = test.RequestCall(101);
  test.Expect(101, true);
  test.Step();
  CoreEnd2endTest::IncomingCloseOnServer client_close;
  s.NewBatch(102)
      .SendInitialMetadata({})
      .SendStatusFromServer(GRPC_STATUS_UNIMPLEMENTED, dragon, {})
      .RecvCloseOnServer(client_close);
  test.Expect(102, true);
  test.Expect(1, true);
  test.Step();
  EXPECT_EQ(server_status.status(), GRPC_STATUS_UNIMPLEMENTED);
  EXPECT_EQ(server_status.message(), dragon);
  EXPECT_EQ(s.method(), method);
  EXPECT_FALSE(client_close.was_cancelled());
}

void HpackSize(CoreEnd2endTest& test, int encode_size, int decode_size) {
  // TODO(ctiller): right now the hpack encoder isn't compressing these, so this
  // test doesn't do what we want - which is to test overflow the hpack table
  // slot count.
  test.InitServer(
      ChannelArgs().Set(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_DECODER, decode_size));
  test.InitClient(
      ChannelArgs().Set(GRPC_ARG_HTTP2_HPACK_TABLE_SIZE_ENCODER, encode_size));
  for (size_t i = 0; i < 4 * hobbits->size(); i++) {
    SimpleRequestBody(test, i);
  }
}

CORE_END2END_TEST(Http2SingleHopTest, Encode0Decode0) {
  HpackSize(*this, 0, 0);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode0Decode100) {
  HpackSize(*this, 0, 100);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode0Decode1000) {
  HpackSize(*this, 0, 1000);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode0Decode4096) {
  HpackSize(*this, 0, 4096);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode0Decode32768) {
  HpackSize(*this, 0, 32768);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode0Decode4194304) {
  HpackSize(*this, 0, 4194304);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode100Decode0) {
  HpackSize(*this, 100, 0);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode100Decode100) {
  HpackSize(*this, 100, 100);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode100Decode1000) {
  HpackSize(*this, 100, 1000);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode100Decode4096) {
  HpackSize(*this, 100, 4096);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode100Decode32768) {
  HpackSize(*this, 100, 32768);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode100Decode4194304) {
  HpackSize(*this, 100, 4194304);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode1000Decode0) {
  HpackSize(*this, 1000, 0);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode1000Decode100) {
  HpackSize(*this, 1000, 100);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode1000Decode1000) {
  HpackSize(*this, 1000, 1000);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode1000Decode4096) {
  HpackSize(*this, 1000, 4096);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode1000Decode32768) {
  HpackSize(*this, 1000, 32768);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode1000Decode4194304) {
  HpackSize(*this, 1000, 4194304);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4096Decode0) {
  HpackSize(*this, 4096, 0);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4096Decode100) {
  HpackSize(*this, 4096, 100);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4096Decode1000) {
  HpackSize(*this, 4096, 1000);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4096Decode4096) {
  HpackSize(*this, 4096, 4096);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4096Decode32768) {
  HpackSize(*this, 4096, 32768);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4096Decode4194304) {
  HpackSize(*this, 4096, 4194304);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode32768Decode0) {
  HpackSize(*this, 32768, 0);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode32768Decode100) {
  HpackSize(*this, 32768, 100);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode32768Decode1000) {
  HpackSize(*this, 32768, 1000);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode32768Decode4096) {
  HpackSize(*this, 32768, 4096);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode32768Decode32768) {
  HpackSize(*this, 32768, 32768);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode32768Decode4194304) {
  HpackSize(*this, 32768, 4194304);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4194304Decode0) {
  HpackSize(*this, 4194304, 0);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4194304Decode100) {
  HpackSize(*this, 4194304, 100);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4194304Decode1000) {
  HpackSize(*this, 4194304, 1000);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4194304Decode4096) {
  HpackSize(*this, 4194304, 4096);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4194304Decode32768) {
  HpackSize(*this, 4194304, 32768);
}
CORE_END2END_TEST(Http2SingleHopTest, Encode4194304Decode4194304) {
  HpackSize(*this, 4194304, 4194304);
}

}  // namespace
}  // namespace grpc_core
