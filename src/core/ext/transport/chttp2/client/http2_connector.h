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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_CLIENT_HTTP2_CONNECTOR_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_CLIENT_HTTP2_CONNECTOR_H

#include "absl/base/thread_annotations.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/client_channel/connector.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"

// TODO(tjagtap) : [PH2_TODO][P1] Remove unused includes after class is ready.

namespace grpc_core {
namespace http {

// Experimental : All code in this file will undergo large scale changes in the
// coming year. Do not use unless you know transports well enough.
// TODO(tjagtap) : [PH2_TODO][P2] : Remove comment when code is ready
class Http2Connector : public SubchannelConnector {
 public:
  void Connect(const Args& args, Result* result, grpc_closure* notify) override;
  void Shutdown(grpc_error_handle error) override;

 private:
  void OnHandshakeDone(absl::StatusOr<HandshakerArgs*> result);
  static void OnReceiveSettings(void* arg, grpc_error_handle error);
  void OnTimeout() ABSL_LOCKS_EXCLUDED(mu_);

  Mutex mu_;
};

}  // namespace http
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_CLIENT_HTTP2_CONNECTOR_H
