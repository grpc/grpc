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

#include "src/cpp/proto/proto_utils.h"
#include <grpc++/config.h>

#include <grpc/grpc.h>
#include <grpc/support/slice.h>

namespace grpc {

bool SerializeProto(const grpc::protobuf::Message &msg,
                    grpc_byte_buffer **bp) {
  grpc::string msg_str;
  bool success = msg.SerializeToString(&msg_str);
  if (success) {
    gpr_slice slice =
        gpr_slice_from_copied_buffer(msg_str.data(), msg_str.length());
    *bp = grpc_byte_buffer_create(&slice, 1);
    gpr_slice_unref(slice);
  }
  return success;
}

bool DeserializeProto(grpc_byte_buffer *buffer,
                      grpc::protobuf::Message *msg) {
  grpc::string msg_string;
  grpc_byte_buffer_reader *reader = grpc_byte_buffer_reader_create(buffer);
  gpr_slice slice;
  while (grpc_byte_buffer_reader_next(reader, &slice)) {
    const char *data = reinterpret_cast<const char *>(
        slice.refcount ? slice.data.refcounted.bytes
                       : slice.data.inlined.bytes);
    msg_string.append(data, slice.refcount ? slice.data.refcounted.length
                                           : slice.data.inlined.length);
    gpr_slice_unref(slice);
  }
  grpc_byte_buffer_reader_destroy(reader);
  return msg->ParseFromString(msg_string);
}

}  // namespace grpc
