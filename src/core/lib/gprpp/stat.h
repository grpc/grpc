//
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPRPP_STAT_H
#define GRPC_CORE_LIB_GPRPP_STAT_H

#include <grpc/support/port_platform.h>

#include <stdio.h>
#include <time.h>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Gets the last-modified timestamp of a file or a directory.
// On success, the correct timestamp will be filled with an StatusCode::kOk
// returned. Otherwise, timestamp will be untouched and an
// StatusCode::kInternal will be returned.
absl::Status GetFileModificationTime(const char* filename, time_t* timestamp);

// A simple helper function that will combine |path1| and |path2| with the file
// separator, and return the new path. It doesn't check the validity of the new
// path.
std::string PathJoin(std::string path1, std::string path2);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_STAT_H
