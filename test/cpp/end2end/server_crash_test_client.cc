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

#include <gflags/gflags.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc/support/log.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"

DEFINE_string(address, "", "Address to connect to");
DEFINE_string(mode, "", "Test mode to use");

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

int main(int argc, char** argv) {
  ParseCommandLineFlags(&argc, &argv, true);
  auto stub = grpc::testing::EchoTestService::NewStub(
      grpc::CreateChannel(FLAGS_address, grpc::InsecureChannelCredentials()));

  EchoRequest request;
  EchoResponse response;
  grpc::ClientContext context;
  context.set_wait_for_ready(true);

  if (FLAGS_mode == "bidi") {
    auto stream = stub->BidiStream(&context);
    for (int i = 0;; i++) {
      std::ostringstream msg;
      msg << "Hello " << i;
      request.set_message(msg.str());
      GPR_ASSERT(stream->Write(request));
      GPR_ASSERT(stream->Read(&response));
      GPR_ASSERT(response.message() == request.message());
    }
  } else if (FLAGS_mode == "response") {
    EchoRequest request;
    request.set_message("Hello");
    auto stream = stub->ResponseStream(&context, request);
    for (;;) {
      GPR_ASSERT(stream->Read(&response));
    }
  } else {
    gpr_log(GPR_ERROR, "invalid test mode '%s'", FLAGS_mode.c_str());
    return 1;
  }

  return 0;
}
