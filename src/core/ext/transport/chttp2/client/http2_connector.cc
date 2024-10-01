//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/ext/transport/chttp2/client/http2_connector.h"

#include <stdint.h>

#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

// TODO(tjagtap) : [PH2_TODO][P1] Remove unused includes after class is ready.

namespace grpc_core {
namespace http {

using ::grpc_event_engine::experimental::EventEngine;

// Experimental : All code in this file will undergo large scale changes in the
// coming year. Do not use unless you know transports well enough.
// TODO(tjagtap) : [PH2_TODO][P2] : Remove comment when code is ready

void Http2Connector::Connect(const Args& args, Result* result,
                             grpc_closure* notify) {}

void Http2Connector::Shutdown(grpc_error_handle error) {}

void Http2Connector::OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result) {}

void Http2Connector::OnReceiveSettings(void* arg, grpc_error_handle error) {}

void Http2Connector::OnTimeout() {}

void Http2Connector::MaybeNotify(grpc_error_handle error) {}

}  // namespace http
}  // namespace grpc_core
