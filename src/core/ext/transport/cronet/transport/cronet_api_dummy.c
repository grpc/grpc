/*
 *
 * Copyright 2016, Google Inc.
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

/* This file has empty implementation of all the functions exposed by the cronet
library, so we can build it in all environments */

#include <stdbool.h>

#include <grpc/support/log.h>

#include "third_party/objective_c/Cronet/bidirectional_stream_c.h"

#ifdef GRPC_COMPILE_WITH_CRONET
/* link with the real CRONET library in the build system */
#else
/* Dummy implementation of cronet API just to test for build-ability */
bidirectional_stream* bidirectional_stream_create(
    stream_engine* engine, void* annotation,
    bidirectional_stream_callback* callback) {
  GPR_ASSERT(0);
  return NULL;
}

int bidirectional_stream_destroy(bidirectional_stream* stream) {
  GPR_ASSERT(0);
  return 0;
}

int bidirectional_stream_start(bidirectional_stream* stream, const char* url,
                               int priority, const char* method,
                               const bidirectional_stream_header_array* headers,
                               bool end_of_stream) {
  GPR_ASSERT(0);
  return 0;
}

int bidirectional_stream_read(bidirectional_stream* stream, char* buffer,
                              int capacity) {
  GPR_ASSERT(0);
  return 0;
}

int bidirectional_stream_write(bidirectional_stream* stream, const char* buffer,
                               int count, bool end_of_stream) {
  GPR_ASSERT(0);
  return 0;
}

void bidirectional_stream_cancel(bidirectional_stream* stream) {
  GPR_ASSERT(0);
}

#endif /* GRPC_COMPILE_WITH_CRONET */
