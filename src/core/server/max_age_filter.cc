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

#include "src/core/server/max_age_filter.h"

#include "src/core/lib/transport/http2_errors.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

MaxAgeFilter::MaxAgeFilter(ConnectionId connection_id, ServerInterface* server)
    : idle_state_(true), connection_id_(connection_id), server_(server) {
  StartTimer();
}

void MaxAgeFilter::Orphaned() { server_->CancelMaxAgeTimer(max_age_timer_); }

void MaxAgeFilter::StartTimer() {
  max_age_timer_ = server_->RunWithNextMaxAgeTimer(
      [self = WeakRef()]() { self->FinishTimer(); });
}

void MaxAgeFilter::FinishTimer() {
  if (idle_state_.CheckTimer()) {
    StartTimer();
  } else {
    RefCountedPtr<Transport> transport = server_->GetTransport(connection_id_);
    if (transport != nullptr) {
      transport->SendGoaway(grpc_error_set_int(GRPC_ERROR_CREATE("max_age"),
                                               StatusIntProperty::kHttp2Error,
                                               GRPC_HTTP2_NO_ERROR));
    }
    server_->RunWithNextMaxAgeGraceTimer(
        [connection_id = connection_id_, server = server_]() {
          server->RemoveTransport(connection_id);
        });
  }
}

}  // namespace grpc_core
