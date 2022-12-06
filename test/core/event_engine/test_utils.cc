// Copyright 2022 gRPC authors.
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
#include <grpc/support/port_platform.h>

#include "test/core/event_engine/test_utils.h"

#include <random>
#include <string>

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

constexpr int kMinMessageSize = 1024;
constexpr int kMaxMessageSize = 4096;

}  // namespace

EventEngine::ResolvedAddress URIToResolvedAddress(
    absl::string_view address_str) {
  grpc_resolved_address addr;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(address_str);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "Failed to parse. Error: %s",
            uri.status().ToString().c_str());
    GPR_ASSERT(uri.ok());
  }
  GPR_ASSERT(grpc_parse_uri(*uri, &addr));
  return EventEngine::ResolvedAddress(
      reinterpret_cast<const sockaddr*>(addr.addr), addr.len);
}

void AppendStringToSliceBuffer(SliceBuffer* buf, absl::string_view data) {
  buf->Append(Slice::FromCopiedString(data));
}

std::string ExtractSliceBufferIntoString(SliceBuffer* buf) {
  if (!buf->Length()) {
    return std::string();
  }
  std::string tmp(buf->Length(), '\0');
  char* bytes = const_cast<char*>(tmp.c_str());
  grpc_slice_buffer_move_first_into_buffer(buf->c_slice_buffer(), buf->Length(),
                                           bytes);
  return tmp;
}

// Returns a random message with bounded length.
std::string GetNextSendMessage() {
  static const char alphanum[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  static std::random_device rd;
  static std::seed_seq seed{rd()};
  static std::mt19937 gen(seed);
  static std::uniform_real_distribution<> dis(kMinMessageSize, kMaxMessageSize);
  static grpc_core::Mutex g_mu;
  std::string tmp_s;
  int len;
  {
    grpc_core::MutexLock lock(&g_mu);
    len = dis(gen);
  }
  tmp_s.reserve(len);
  for (int i = 0; i < len; ++i) {
    tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
  }
  return tmp_s;
}

}  // namespace experimental
}  // namespace grpc_event_engine
