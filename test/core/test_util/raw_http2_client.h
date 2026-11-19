//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_TEST_CORE_TEST_UTIL_RAW_HTTP2_CLIENT_H
#define GRPC_TEST_CORE_TEST_UTIL_RAW_HTTP2_CLIENT_H

#include <grpc/event_engine/event_engine.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

// A minimal plaintext HTTP/2 client that writes raw frames. This lets a test
// send request headers that a normal gRPC client never sends, such as both
// "host" and ":authority" with different values.
class RawHttp2Client {
 public:
  struct Header {
    std::string key;
    std::string value;
  };

  // Connects to the given port on localhost. Crashes on failure.
  explicit RawHttp2Client(int port);

  // Sends a unary gRPC request on stream 1, then waits for the server to end
  // the stream. Returns the RPC status from the grpc-status and grpc-message
  // trailers. If the stream or connection ends without trailers, returns an
  // INTERNAL error whose message starts with "RawHttp2Client:".
  //
  // The ":method", ":scheme", "content-type" and "te" headers are always
  // sent with their standard gRPC values. `path` is sent as ":path".
  // `headers` are sent after ":path" and before "content-type", in order,
  // so any pseudo-headers in `headers` (such as ":authority") must come
  // before any regular headers (such as "host").
  //
  // Must be called at most once per instance, since it sends the connection
  // preface and always uses stream 1.
  absl::Status SendUnaryRequest(absl::string_view path,
                                const std::vector<Header>& headers,
                                absl::string_view serialized_request);

 private:
  // Returns false on error or timeout.
  bool Write(absl::string_view data);
  // Returns false on EOF, error, or timeout.
  bool ReadExact(size_t n, std::string* out);

  bool request_sent_ = false;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_;
  // Bytes read from the endpoint but not yet returned by ReadExact().
  std::string read_buffer_;
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint_;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TEST_UTIL_RAW_HTTP2_CLIENT_H
