//
//
// Copyright 2016 gRPC authors.
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

// This file has empty implementation of all the functions exposed by the cronet
// library, so we can build it in all environments

#include <grpc/support/port_platform.h>

#include "third_party/objective_c/Cronet/bidirectional_stream_c.h"

#include <grpc/support/log.h>

#ifdef GRPC_COMPILE_WITH_CRONET
// link with the real CRONET library in the build system
#else
// Phony implementation of cronet API just to test for build-ability
bidirectional_stream* bidirectional_stream_create(
    stream_engine* /*engine*/, void* /*annotation*/,
    bidirectional_stream_callback* /*callback*/) {
  GPR_ASSERT(0);
  return nullptr;
}

int bidirectional_stream_destroy(bidirectional_stream* /*stream*/) {
  GPR_ASSERT(0);
  return 0;
}

int bidirectional_stream_start(
    bidirectional_stream* /*stream*/, const char* /*url*/, int /*priority*/,
    const char* /*method*/,
    const bidirectional_stream_header_array* /*headers*/,
    bool /*end_of_stream*/) {
  GPR_ASSERT(0);
  return 0;
}

int bidirectional_stream_read(bidirectional_stream* /*stream*/,
                              char* /*buffer*/, int /*capacity*/) {
  GPR_ASSERT(0);
  return 0;
}

int bidirectional_stream_write(bidirectional_stream* /*stream*/,
                               const char* /*buffer*/, int /*count*/,
                               bool /*end_of_stream*/) {
  GPR_ASSERT(0);
  return 0;
}

void bidirectional_stream_cancel(bidirectional_stream* /*stream*/) {
  GPR_ASSERT(0);
}

void bidirectional_stream_disable_auto_flush(bidirectional_stream* /*stream*/,
                                             bool /*disable_auto_flush*/) {
  GPR_ASSERT(0);
}

void bidirectional_stream_delay_request_headers_until_flush(
    bidirectional_stream* /*stream*/, bool /*delay_headers_until_flush*/) {
  GPR_ASSERT(0);
}

void bidirectional_stream_flush(bidirectional_stream* /*stream*/) {
  GPR_ASSERT(0);
}

#endif  // GRPC_COMPILE_WITH_CRONET
