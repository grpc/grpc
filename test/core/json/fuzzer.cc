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

#include <stdint.h>
#include <string.h>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/support/json.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"

bool squelch = true;
bool leak_check = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto json = grpc_core::JsonParse(
      absl::string_view(reinterpret_cast<const char*>(data), size));
  if (json.ok()) {
    auto text2 = grpc_core::JsonDump(*json);
    auto json2 = grpc_core::JsonParse(text2);
    GPR_ASSERT(json2.ok());
    GPR_ASSERT(*json == *json2);
  }
  return 0;
}
