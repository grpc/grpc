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

#include "absl/strings/str_cat.h"

#include <grpcpp/impl/codegen/core_codegen.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"

namespace grpc {

const Status& CoreCodegen::ok() { return grpc::Status::OK; }

const Status& CoreCodegen::cancelled() { return grpc::Status::CANCELLED; }

void CoreCodegen::assert_fail(const char* failed_assertion, const char* file,
                              int line) {
  grpc_core::Crash(absl::StrCat("Assertion failed: ", failed_assertion),
                   grpc_core::SourceLocation(file, line));
}

}  // namespace grpc
