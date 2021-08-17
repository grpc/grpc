/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/surface/event_string.h"

#include <stdio.h>

#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/byte_buffer.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/string.h"

static void addhdr(grpc_event* ev, std::vector<std::string>* buf) {
  buf->push_back(absl::StrFormat("tag:%p", ev->tag));
}

static const char* errstr(int success) { return success ? "OK" : "ERROR"; }

static void adderr(int success, std::vector<std::string>* buf) {
  buf->push_back(absl::StrFormat(" %s", errstr(success)));
}

std::string grpc_event_string(grpc_event* ev) {
  if (ev == nullptr) return "null";
  std::vector<std::string> out;
  switch (ev->type) {
    case GRPC_QUEUE_TIMEOUT:
      out.push_back("QUEUE_TIMEOUT");
      break;
    case GRPC_QUEUE_SHUTDOWN:
      out.push_back("QUEUE_SHUTDOWN");
      break;
    case GRPC_OP_COMPLETE:
      out.push_back("OP_COMPLETE: ");
      addhdr(ev, &out);
      adderr(ev->success, &out);
      break;
  }
  return absl::StrJoin(out, "");
}
