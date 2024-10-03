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

#include "src/core/ext/transport/http2_server_transport.h"

#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace http2 {

void Http2ServerTransport::SetCallDestination(
    RefCountedPtr<UnstartedCallDestination> call_destination) {}

void Http2ServerTransport::PerformOp(grpc_transport_op*) {}

void Http2ServerTransport::AbortWithError() {}

void Http2ServerTransport::Orphan() {}

}  // namespace http2
}  // namespace grpc_core
