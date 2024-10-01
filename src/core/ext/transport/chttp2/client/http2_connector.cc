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

#include <stdint.h>

#include <string>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/grpc_posix.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice_buffer.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>

#include "src/core/channelz/channelz.h"
#include "src/core/client_channel/client_channel_factory.h"
#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/client_channel/connector.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/handshaker/tcp_connect/tcp_connect_handshaker.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/insecure/insecure_credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_create.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"
#include "src/core/util/unique_type_name.h"

#ifdef GPR_SUPPORT_CHANNELS_FROM_FD

#include <fcntl.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/tcp_client_posix.h"

#endif  // GPR_SUPPORT_CHANNELS_FROM_FD

// TODO(tjagtap) : [PH2_TODO][P1] Remove unused includes after class is ready.
// Compiler whining about missing includes when we are iterating over the code
// is a huge time sink.

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
