//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CORE_UTIL_UPB_UTILS_H
#define GRPC_SRC_CORE_UTIL_UPB_UTILS_H

#include <grpc/support/time.h>

#include <string>

#include "absl/strings/string_view.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"

namespace grpc_core {

// Works for both std::string and absl::string_view.
template <typename T>
inline upb_StringView StdStringToUpbString(const T& str) {
  return upb_StringView_FromDataAndSize(str.data(), str.size());
}

inline upb_StringView StdStringToUpbString(const char* str) {
  return upb_StringView_FromDataAndSize(str, strlen(str));
}

template <typename T>
inline upb_StringView CopyStdStringToUpbString(const T& str, upb_Arena* arena) {
  char* copy = static_cast<char*>(upb_Arena_Malloc(arena, str.size()));
  memcpy(copy, str.data(), str.size());
  return upb_StringView_FromDataAndSize(copy, str.size());
}

inline absl::string_view UpbStringToAbsl(const upb_StringView& str) {
  return absl::string_view(str.data, str.size);
}

inline std::string UpbStringToStdString(const upb_StringView& str) {
  return std::string(str.data, str.size);
}

inline void TimestampToUpb(gpr_timespec ts, google_protobuf_Timestamp* proto) {
  auto t = gpr_convert_clock_type(ts, GPR_CLOCK_REALTIME);
  google_protobuf_Timestamp_set_seconds(proto, t.tv_sec);
  google_protobuf_Timestamp_set_nanos(proto, t.tv_nsec);
}

inline void UpbToTimestamp(const google_protobuf_Timestamp* proto,
                           gpr_timespec* ts) {
  ts->clock_type = GPR_CLOCK_REALTIME;
  ts->tv_sec = google_protobuf_Timestamp_seconds(proto);
  ts->tv_nsec = google_protobuf_Timestamp_nanos(proto);
}

inline void DurationToUpb(gpr_timespec ts, google_protobuf_Duration* proto) {
  auto t = gpr_convert_clock_type(ts, GPR_TIMESPAN);
  google_protobuf_Duration_set_seconds(proto, t.tv_sec);
  google_protobuf_Duration_set_nanos(proto, t.tv_nsec);
}

inline void UpbToTimestamp(const google_protobuf_Duration* proto,
                           gpr_timespec* ts) {
  ts->clock_type = GPR_TIMESPAN;
  ts->tv_sec = google_protobuf_Duration_seconds(proto);
  ts->tv_nsec = google_protobuf_Duration_nanos(proto);
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_UPB_UTILS_H
