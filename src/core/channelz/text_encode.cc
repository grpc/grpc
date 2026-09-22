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

#include "src/core/channelz/text_encode.h"

#include <string>

#include "src/core/util/no_destruct.h"
#include "src/proto/grpc/channelz/v2/channelz.upbdefs.h"
#include "src/proto/grpc/channelz/v2/property_list.upbdefs.h"
#include "src/proto/grpc/channelz/v2/service.upbdefs.h"
#include "upb/mem/arena.h"
#include "upb/reflection/def.hpp"
#include "upb/text/encode.h"

namespace grpc_core::channelz {

namespace {

upb_DefPool* DefPool() {
  static NoDestruct<upb::DefPool> def_pool{[]() {
    upb::DefPool pool;
    grpc_channelz_v2_Entity_getmsgdef(pool.ptr());
    grpc_channelz_v2_PropertyList_getmsgdef(pool.ptr());
    grpc_channelz_v2_PropertyGrid_getmsgdef(pool.ptr());
    grpc_channelz_v2_PropertyTable_getmsgdef(pool.ptr());
    grpc_channelz_v2_QueryEntitiesRequest_getmsgdef(pool.ptr());
    grpc_channelz_v2_QueryEntitiesResponse_getmsgdef(pool.ptr());
    grpc_channelz_v2_GetEntityRequest_getmsgdef(pool.ptr());
    grpc_channelz_v2_GetEntityResponse_getmsgdef(pool.ptr());
    grpc_channelz_v2_QueryTraceRequest_getmsgdef(pool.ptr());
    grpc_channelz_v2_QueryTraceResponse_getmsgdef(pool.ptr());
    grpc_channelz_v2_TraceEvent_getmsgdef(pool.ptr());
    return pool;
  }()};
  return def_pool->ptr();
}

}  // namespace

std::string TextEncode(upb_Message* message,
                       const upb_MessageDef* (*getmsgdef)(upb_DefPool*)) {
  char buf[10240];
  auto* def_pool = DefPool();
  auto* def = getmsgdef(def_pool);
  size_t size = upb_TextEncode(message, def, def_pool, 0, buf, sizeof(buf));
  if (size < sizeof(buf)) return std::string(buf, size);
  // upb_TextEncode has snprintf() semantics: it always NUL-terminates
  // within the given capacity, so writing size content bytes requires
  // a capacity of size + 1. We allocate size + 1 bytes in the string,
  // let upb_TextEncode write the content and the NUL terminator, and then
  // resize the string back to size to remove the NUL terminator from the
  // string's active length.
  std::string result(size + 1, '\0');
  upb_TextEncode(message, def, def_pool, 0, result.data(), result.size());
  result.resize(size);
  return result;
}

}  // namespace grpc_core::channelz
