//
// Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/retry_filter.h"

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "retry_filter.h"
#include "retry_service_config.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/retry_filter_legacy_call_data.h"
#include "src/core/ext/filters/client_channel/retry_service_config.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/service_config/service_config.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/uri/uri_parser.h"

//
// Retry filter
//

// This filter is intended to be used in the DynamicFilter stack in the
// client channel, which is situated between the name resolver and the
// LB policy.  Normally, the last filter in the DynamicFilter stack is
// the DynamicTerminationFilter (see client_channel.cc), which creates a
// LoadBalancedCall and delegates to it.  However, when retries are
// enabled, this filter is used instead of the DynamicTerminationFilter.
//
// In order to support retries, we act as a proxy for stream op batches.
// When we get a batch from the surface, we add it to our list of pending
// batches, and we then use those batches to construct separate "child"
// batches to be started on an LB call.  When the child batches return, we
// then decide which pending batches have been completed and schedule their
// callbacks accordingly.  If a call attempt fails and we want to retry it,
// we create a new LB call and start again, constructing new "child" batches
// for the new LB call.
//
// Note that retries are committed when receiving data from the server
// (except for Trailers-Only responses).  However, there may be many
// send ops started before receiving any data, so we may have already
// completed some number of send ops (and returned the completions up to
// the surface) by the time we realize that we need to retry.  To deal
// with this, we cache data for send ops, so that we can replay them on a
// different LB call even after we have completed the original batches.
//
// The code is structured as follows:
// - In CallData (in the parent channel), we maintain a list of pending
//   ops and cached data for send ops.
// - There is a CallData::CallAttempt object for each retry attempt.
//   This object contains the LB call for that attempt and state to indicate
//   which ops from the CallData object have already been sent down to that
//   LB call.
// - There is a CallData::CallAttempt::BatchData object for each "child"
//   batch sent on the LB call.
//
// When constructing the "child" batches, we compare the state in the
// CallAttempt object against the state in the CallData object to see
// which batches need to be sent on the LB call for a given attempt.

// TODO(roth): In subsequent PRs:
// - implement hedging

using grpc_core::internal::RetryGlobalConfig;
using grpc_core::internal::RetryMethodConfig;
using grpc_core::internal::RetryServiceConfigParser;
using grpc_event_engine::experimental::EventEngine;

grpc_core::TraceFlag grpc_retry_trace(false, "retry");

namespace grpc_core {

//
// RetryFilter
//

RetryFilter::RetryFilter(const ChannelArgs& args, grpc_error_handle* error)
    : client_channel_(args.GetObject<ClientChannel>()),
      event_engine_(args.GetObject<EventEngine>()),
      per_rpc_retry_buffer_size_(GetMaxPerRpcRetryBufferSize(args)),
      service_config_parser_index_(
          internal::RetryServiceConfigParser::ParserIndex()) {
  // Get retry throttling parameters from service config.
  auto* service_config = args.GetObject<ServiceConfig>();
  if (service_config == nullptr) return;
  const auto* config = static_cast<const RetryGlobalConfig*>(
      service_config->GetGlobalParsedConfig(
          RetryServiceConfigParser::ParserIndex()));
  if (config == nullptr) return;
  // Get server name from target URI.
  auto server_uri = args.GetString(GRPC_ARG_SERVER_URI);
  if (!server_uri.has_value()) {
    *error = GRPC_ERROR_CREATE(
        "server URI channel arg missing or wrong type in client channel "
        "filter");
    return;
  }
  absl::StatusOr<URI> uri = URI::Parse(*server_uri);
  if (!uri.ok() || uri->path().empty()) {
    *error = GRPC_ERROR_CREATE("could not extract server name from target URI");
    return;
  }
  std::string server_name(absl::StripPrefix(uri->path(), "/"));
  // Get throttling config for server_name.
  retry_throttle_data_ =
      internal::ServerRetryThrottleMap::Get()->GetDataForServer(
          server_name, config->max_milli_tokens(), config->milli_token_ratio());
}

const RetryMethodConfig* RetryFilter::GetRetryPolicy(
    const grpc_call_context_element* context) {
  if (context == nullptr) return nullptr;
  auto* svc_cfg_call_data = static_cast<ServiceConfigCallData*>(
      context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
  if (svc_cfg_call_data == nullptr) return nullptr;
  return static_cast<const RetryMethodConfig*>(
      svc_cfg_call_data->GetMethodParsedConfig(service_config_parser_index_));
}

class RetryFilter::MessageForwarder {
 public:
  void Push(MessageHandle msg) { messages_.emplace_back(std::move(msg)); }

  class Listener {
   public:
    auto Next() {
      return []() -> Poll<absl::optional<MessageHandle>> { abort(); };
    }
  };

 private:
  std::vector<MessageHandle> messages_;
};

struct RetryFilter::CallState {
  bool sent_transparent_retry_not_seen_by_server = false;
};

struct RetryFilter::CallAttemptState {
  explicit CallAttemptState(RetryFilter* filter)
      : retry_policy(
            filter->GetRetryPolicy(GetContext<grpc_call_context_element>())) {}
  const RetryMethodConfig* const retry_policy;
  Latch<absl::Status> early_return;
};

absl::optional<Duration> RetryFilter::MaybeRetryDuration(
    CallState* call_state, CallAttemptState* call_attempt,
    ServerMetadataHandle md, bool committed) {
  // TODO(ctiller): how to get lb drop detail here
  enum { kNoRetry, kTransparentRetry, kConfigurableRetry } retry = kNoRetry;
  // Handle transparent retries.
  const auto stream_network_state = md->get<GrpcStreamNetworkState>();
  if (stream_network_state.has_value() && !committed) {
    // If not sent on wire, then always retry.
    // If sent on wire but not seen by server, retry exactly once.
    if (*stream_network_state == GrpcStreamNetworkState::kNotSentOnWire) {
      retry = kTransparentRetry;
    } else if (*stream_network_state ==
                   GrpcStreamNetworkState::kNotSeenByServer &&
               !call_state->sent_transparent_retry_not_seen_by_server) {
      call_state->sent_transparent_retry_not_seen_by_server = true;
      retry = kTransparentRetry;
    }
  }
  // If not transparently retrying, check for configurable retry.
  const auto server_pushback = md->get(GrpcRetryPushbackMsMetadata());
  if (retry == kNoRetry && call_attempt->ShouldRetry(status, server_pushback)) {
    retry = kConfigurableRetry;
  }
  switch (retry) {
    case kNoRetry:
      return absl::nullopt;
    case kTransparentRetry:
      return Duration::Zero();
    case kConfigurableRetry:
      return server_pushback.value_or(Duration::Zero());
  }
}

auto RetryFilter::MakeCallAttempt(
    bool& committed, MessageForwarder& message_forwarder,
    const ClientMetadataHandle& initial_metadata) {
  auto* party = static_cast<Party*>(Activity::current());
  CallArgs child_call_args{};
  auto child_call = client_channel()->CreateLoadBalancedCallPromise(
      std::move(child_call_args), []() { Crash("on_commit not implemented"); },
      false);
  auto* attempt = new CallAttemptState(this);
  // If per_attempt_recv_timeout is set, start a timer.
  if (attempt->retry_policy != nullptr &&
      attempt->retry_policy->per_attempt_recv_timeout().has_value()) {
    party->Spawn(
        "per_attempt_recv_timeout",
        Sleep(Timestamp::Now() +
              attempt->retry_policy->per_attempt_recv_timeout().value()),
        [attempt](absl::Status) {
          if (!attempt->early_return.is_set()) {
            attempt->early_return.Set(
                absl::CancelledError("retry perAttemptRecvTimeout exceeded"));
          }
        });
  }
  return Seq(
      TryJoin(attempt->early_return.Wait(),
              ForEach(MessageForwarder::Listener(),
                      [](MessageHandle msg) {
                        return client_to_server_messages->Push(std::move(msg));
                      }),
              Seq(child_call_receive_initial_metadata->Next(),
                  [&committed](ServerMetadataHandle md) mutable {
                    committed = true;
                  }),
              ForEach(child_call_receive_messages,
                      [](MessageHandle msg) {
                        return server_to_client_messages->Push(std::move(msg));
                      }),
              std::move(child_call)),
      [this, &committed](ServerMetadataHandle result) {
        auto maybe_retry_duration = MaybeRetryDuration(result, committed);
        return If(
            maybe_retry_duration.has_value(),
            [maybe_retry_duration]() {
              return Seq(Sleep(Timestamp::Now() + *maybe_retry_duration),
                         []() { return Continue(); });
            },
            [result = std::move(result)]() mutable {
              return std::move(result);
            });
      });
}

ArenaPromise<ServerMetadataHandle> RetryFilter::MakeCallPromise(
    CallArgs call_args) {
  return Loop([this, committed = false, message_forwarder = MessageForwarder(),
               initial_metadata =
                   std::move(call_args.client_initial_metadata)]() mutable {
    return MakeCallAttempt(committed, message_forwarder, initial_metadata);
  });
}

const grpc_channel_filter RetryFilter::kVtable = {
    RetryFilter::LegacyCallData::StartTransportStreamOpBatch,
    [](grpc_channel_element* elem, grpc_core::CallArgs call_args,
       grpc_core::NextPromiseFactory) {
      return static_cast<RetryFilter*>(elem->channel_data)
          ->MakeCallPromise(std::move(call_args));
    },
    RetryFilter::StartTransportOp,
    sizeof(RetryFilter::LegacyCallData),
    RetryFilter::LegacyCallData::Init,
    RetryFilter::LegacyCallData::SetPollent,
    RetryFilter::LegacyCallData::Destroy,
    sizeof(RetryFilter),
    RetryFilter::Init,
    grpc_channel_stack_no_post_init,
    RetryFilter::Destroy,
    RetryFilter::GetChannelInfo,
    "retry_filter",
};

}  // namespace grpc_core
