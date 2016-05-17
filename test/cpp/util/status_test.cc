/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/support/status.h>

#include <grpc/status.h>
#include <grpc/support/log.h>

// Make sure the existing grpc_status_code match with grpc::Code.
int main(int argc, char** argv) {
  GPR_ASSERT(grpc::StatusCode::OK ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_OK));
  GPR_ASSERT(grpc::StatusCode::CANCELLED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_CANCELLED));
  GPR_ASSERT(grpc::StatusCode::UNKNOWN ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNKNOWN));
  GPR_ASSERT(grpc::StatusCode::INVALID_ARGUMENT ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_INVALID_ARGUMENT));
  GPR_ASSERT(grpc::StatusCode::DEADLINE_EXCEEDED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_DEADLINE_EXCEEDED));
  GPR_ASSERT(grpc::StatusCode::NOT_FOUND ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_NOT_FOUND));
  GPR_ASSERT(grpc::StatusCode::ALREADY_EXISTS ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_ALREADY_EXISTS));
  GPR_ASSERT(grpc::StatusCode::PERMISSION_DENIED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_PERMISSION_DENIED));
  GPR_ASSERT(grpc::StatusCode::UNAUTHENTICATED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNAUTHENTICATED));
  GPR_ASSERT(grpc::StatusCode::RESOURCE_EXHAUSTED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_RESOURCE_EXHAUSTED));
  GPR_ASSERT(grpc::StatusCode::FAILED_PRECONDITION ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_FAILED_PRECONDITION));
  GPR_ASSERT(grpc::StatusCode::ABORTED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_ABORTED));
  GPR_ASSERT(grpc::StatusCode::OUT_OF_RANGE ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_OUT_OF_RANGE));
  GPR_ASSERT(grpc::StatusCode::UNIMPLEMENTED ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNIMPLEMENTED));
  GPR_ASSERT(grpc::StatusCode::INTERNAL ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_INTERNAL));
  GPR_ASSERT(grpc::StatusCode::UNAVAILABLE ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_UNAVAILABLE));
  GPR_ASSERT(grpc::StatusCode::DATA_LOSS ==
             static_cast<grpc::StatusCode>(GRPC_STATUS_DATA_LOSS));

  return 0;
}
