/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"

std::string grpc_get_well_known_google_credentials_file_path_impl(void) {
  auto base = grpc_core::GetEnv(GRPC_GOOGLE_CREDENTIALS_PATH_ENV_VAR);
  if (!base.has_value()) {
    gpr_log(GPR_ERROR, "Could not get " GRPC_GOOGLE_CREDENTIALS_PATH_ENV_VAR
                       " environment variable.");
    return "";
  }
  return absl::StrCat(*base, "/", GRPC_GOOGLE_CREDENTIALS_PATH_SUFFIX);
}
