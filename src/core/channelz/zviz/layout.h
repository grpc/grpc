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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_H
#define GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_H

#include <string>

#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "src/core/channelz/zviz/environment.h"

namespace grpc_zviz::layout {

enum class Intent {
  kBanner,
  kHeading,
  kEntityRef,
  kTrace,
  kTraceDescription,
  kData,
  kTimestamp,
  kDuration,
  kNote,
  kKey,
  kValue,
};

template <typename Sink>
void AbslStringify(Sink& sink, Intent intent) {
  switch (intent) {
    case Intent::kBanner:
      sink.Append("banner");
      break;
    case Intent::kHeading:
      sink.Append("heading");
      break;
    case Intent::kEntityRef:
      sink.Append("entity_ref");
      break;
    case Intent::kTrace:
      sink.Append("trace");
      break;
    case Intent::kTraceDescription:
      sink.Append("trace_description");
      break;
    case Intent::kTimestamp:
      sink.Append("timestamp");
      break;
    case Intent::kDuration:
      sink.Append("duration");
      break;
    case Intent::kNote:
      sink.Append("note");
      break;
    case Intent::kData:
      sink.Append("data");
      break;
    case Intent::kKey:
      sink.Append("key");
      break;
    case Intent::kValue:
      sink.Append("value");
      break;
  }
}

enum class TableIntent {
  kTrace,
  kPropertyList,
  kPropertyGrid,
};

template <typename Sink>
void AbslStringify(Sink& sink, TableIntent intent) {
  switch (intent) {
    case TableIntent::kTrace:
      sink.Append("trace");
      break;
    case TableIntent::kPropertyList:
      sink.Append("property_list");
      break;
    case TableIntent::kPropertyGrid:
      sink.Append("property_grid");
      break;
  }
}

class Table;

class Element {
 public:
  virtual ~Element() = default;

  // Append an element and return *this.
  virtual Element& AppendText(Intent intent, absl::string_view text) = 0;
  virtual Element& AppendLink(Intent intent, absl::string_view text,
                              absl::string_view href) = 0;
  // Helpers to append common intents.
  Element& AppendTimestamp(google::protobuf::Timestamp timestamp);
  Element& AppendDuration(google::protobuf::Duration duration);
  Element& AppendEntityLink(Environment& env, int64_t entity_id);
  // These create new groups and return a reference to the new group.
  virtual Element& AppendGroup(Intent intent) = 0;
  virtual Element& AppendData(absl::string_view name,
                              absl::string_view type) = 0;
  virtual Table& AppendTable(TableIntent intent) = 0;
};

class Table {
 public:
  virtual ~Table() = default;
  virtual Element& AppendColumn() = 0;
  virtual void NewRow() = 0;
};

}  // namespace grpc_zviz::layout

#endif  // GRPC_SRC_CORE_CHANNELZ_ZVIZ_LAYOUT_H
