// Copyright 2021 gRPC authors.
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

#include "src/core/call/metadata_batch.h"

#include <grpc/support/port_platform.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "src/core/lib/transport/timeout_encoding.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace grpc_core {

bool IsMetadataKeyAllowedInDebugOutput(absl::string_view key) {
  if (key == ContentTypeMetadata::key()) return true;
  if (key == EndpointLoadMetricsBinMetadata::key()) return true;
  if (key == GrpcAcceptEncodingMetadata::key()) return true;
  if (key == GrpcEncodingMetadata::key()) return true;
  if (key == GrpcInternalEncodingRequest::key()) return true;
  if (key == GrpcLbClientStatsMetadata::key()) return true;
  if (key == GrpcMessageMetadata::key()) return true;
  if (key == GrpcPreviousRpcAttemptsMetadata::key()) return true;
  if (key == GrpcRetryPushbackMsMetadata::key()) return true;
  if (key == GrpcServerStatsBinMetadata::key()) return true;
  if (key == GrpcStatusMetadata::key()) return true;
  if (key == GrpcTagsBinMetadata::key()) return true;
  if (key == GrpcTimeoutMetadata::key()) return true;
  if (key == GrpcTraceBinMetadata::key()) return true;
  if (key == HostMetadata::key()) return true;
  if (key == HttpAuthorityMetadata::key()) return true;
  if (key == HttpMethodMetadata::key()) return true;
  if (key == HttpPathMetadata::key()) return true;
  if (key == HttpSchemeMetadata::key()) return true;
  if (key == HttpStatusMetadata::key()) return true;
  if (key == LbCostBinMetadata::key()) return true;
  if (key == LbTokenMetadata::key()) return true;
  if (key == TeMetadata::key()) return true;
  if (key == UserAgentMetadata::key()) return true;
  if (key == W3CTraceParentMetadata::key()) return true;
  if (key == XEnvoyPeerMetadata::key()) return true;
  if (key == XForwardedForMetadata::key()) return true;
  if (key == XForwardedHostMetadata::key()) return true;
  if (key == GrpcCallWasCancelled::DebugKey()) return true;
  if (key == GrpcRegisteredMethod::DebugKey()) return true;
  if (key == GrpcStatusContext::DebugKey()) return true;
  if (key == GrpcStatusFromWire::DebugKey()) return true;
  if (key == GrpcStreamNetworkState::DebugKey()) return true;
  if (key == GrpcTarPit::DebugKey()) return true;
  if (key == GrpcTrailersOnly::DebugKey()) return true;
  if (key == PeerString::DebugKey()) return true;
  if (key == WaitForReady::DebugKey()) return true;
  return false;
}

}  // namespace grpc_core
