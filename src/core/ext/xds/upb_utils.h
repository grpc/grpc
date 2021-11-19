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

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/string_view.h"
#include "upb/upb.h"

namespace grpc_core {

// Works for both std::string and absl::string_view.
template <typename T>
inline upb_strview StdStringToUpbString(const T& str) {
  return upb_strview_make(str.data(), str.size());
}

inline absl::string_view UpbStringToAbsl(const upb_strview& str) {
  return absl::string_view(str.data, str.size);
}

inline std::string UpbStringToStdString(const upb_strview& str) {
  return std::string(str.data, str.size);
}

}  // namespace grpc_core
