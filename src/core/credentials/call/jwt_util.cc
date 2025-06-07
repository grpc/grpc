//
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
//

#include "src/core/credentials/call/jwt_util.h"

#include <grpc/support/time.h>

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"
#include "src/core/util/json/json_reader.h"

namespace grpc_core {

absl::StatusOr<Timestamp> GetJwtExpirationTime(absl::string_view jwt) {
  // First, split the 3 '.'-delimited parts.
  std::vector<absl::string_view> parts = absl::StrSplit(jwt, '.');
  if (parts.size() != 3) {
    return absl::UnauthenticatedError("error parsing JWT token");
  }
  // Base64-decode the payload.
  std::string payload;
  if (!absl::WebSafeBase64Unescape(parts[1], &payload)) {
    return absl::UnauthenticatedError("error parsing JWT token");
  }
  // Parse as JSON.
  auto json = JsonParse(payload);
  if (!json.ok()) {
    return absl::UnauthenticatedError("error parsing JWT token");
  }
  // Extract "exp" field.
  struct ParsedPayload {
    uint64_t exp = 0;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto kJsonLoader = JsonObjectLoader<ParsedPayload>()
                                          .Field("exp", &ParsedPayload::exp)
                                          .Finish();
      return kJsonLoader;
    }
  };
  auto parsed_payload = LoadFromJson<ParsedPayload>(*json, JsonArgs(), "");
  if (!parsed_payload.ok()) {
    return absl::UnauthenticatedError("error parsing JWT token");
  }
  gpr_timespec ts = gpr_time_0(GPR_CLOCK_REALTIME);
  ts.tv_sec = parsed_payload->exp;
  return Timestamp::FromTimespecRoundDown(ts);
}

}  // namespace grpc_core
