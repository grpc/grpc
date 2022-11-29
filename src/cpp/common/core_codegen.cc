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

#include <stdlib.h>

#include <grpc/byte_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/codegen/gpr_types.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpcpp/impl/codegen/core_codegen.h>
#include <grpcpp/support/status.h>

namespace grpc {

const Status& CoreCodegen::ok() { return grpc::Status::OK; }

const Status& CoreCodegen::cancelled() { return grpc::Status::CANCELLED; }

gpr_timespec CoreCodegen::gpr_inf_future(gpr_clock_type type) {
  return ::gpr_inf_future(type);
}

gpr_timespec CoreCodegen::gpr_time_0(gpr_clock_type type) {
  return ::gpr_time_0(type);
}

void CoreCodegen::assert_fail(const char* failed_assertion, const char* file,
                              int line) {
  gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "assertion failed: %s",
          failed_assertion);
  abort();
}

}  // namespace grpc
