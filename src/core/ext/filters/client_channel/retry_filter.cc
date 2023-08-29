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
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
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

namespace {

class MessageForwarder {
 public:
  auto Push(MessageHandle msg) {
    return [this, msg = std::move(msg)]() mutable -> Poll<Empty> {
      GPR_ASSERT(!closed_);
      if (committed_ && !buffered_messages_.empty()) {
        return waiting_for_write_.pending();
      }
      buffered_messages_.emplace_back(std::move(msg));
      waiting_for_read_.Wake();
      return Empty{};
    };
  }

  class Listener {
   public:
    explicit Listener(MessageForwarder& forwarder) : forwarder_(forwarder) {}
    auto Next() {
      return [this]() -> Poll<absl::optional<MessageHandle>> {
        if (next_ == forwarder_.buffered_messages_.size()) {
          if (forwarder_.closed_) {
            return absl::nullopt;
          } else {
            return forwarder_.waiting_for_read_.pending();
          }
        }
        auto msg = std::move(forwarder_.buffered_messages_[next_++]);
        if (forwarder_.committed_ &&
            next_ == forwarder_.buffered_messages_.size()) {
          forwarder_.buffered_messages_.clear();
          next_ = 0;
        }
        return msg;
      };
    }

   private:
    MessageForwarder& forwarder_;
    size_t next_ = 0;
  };

  bool committed() const { return committed_; }
  void Commit() { committed_ = true; }

 private:
  std::vector<MessageHandle> buffered_messages_;
  IntraActivityWaiter waiting_for_read_;
  IntraActivityWaiter waiting_for_write_;
  bool closed_ = false;
  bool committed_ = false;
};
}  // namespace

struct RetryFilter::CallState {
  CallState(PipeSender<ServerMetadataHandle>* server_initial_metadata,
            PipeSender<MessageHandle>* server_to_client_messages)
      : server_initial_metadata(server_initial_metadata),
        server_to_client_messages(server_to_client_messages) {}
  PipeSender<ServerMetadataHandle>* const server_initial_metadata;
  PipeSender<MessageHandle>* const server_to_client_messages;
  bool sent_transparent_retry_not_seen_by_server = false;
  int num_attempts_completed = 0;
  int num_attempts_started = 0;
  MessageForwarder forwarder;
};

struct RetryFilter::CallAttemptState {
  CallAttemptState(RetryFilter* filter, int attempt)
      : attempt(attempt),
        retry_policy(
            filter->GetRetryPolicy(GetContext<grpc_call_context_element>())),
        retry_throttle_data(filter->retry_throttle_data()) {}
  const int attempt;
  const RetryMethodConfig* const retry_policy;
  const RefCountedPtr<internal::ServerRetryThrottleData> retry_throttle_data;
  Latch<ServerMetadataHandle> early_return;
  Pipe<ServerMetadataHandle> server_initial_metadata;
  Pipe<MessageHandle> server_to_client;
  Pipe<MessageHandle> client_to_server;

  std::string DebugTag() const {
    return absl::StrCat(Activity::current()->DebugTag(), " attempt=", attempt);
  }
  bool ShouldRetry(CallState* calld, absl::optional<grpc_status_code> status,
                   absl::optional<Duration> server_pushback) const;
};

bool RetryFilter::CallAttemptState::ShouldRetry(
    CallState* calld, absl::optional<grpc_status_code> status,
    absl::optional<Duration> server_pushback) const {
  // If no retry policy, don't retry.
  if (retry_policy == nullptr) return false;
  // Check status.
  if (status.has_value()) {
    if (GPR_LIKELY(*status == GRPC_STATUS_OK)) {
      if (retry_throttle_data != nullptr) {
        retry_throttle_data->RecordSuccess();
      }
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "%s: call succeeded", DebugTag().c_str());
      }
      return false;
    }
    // Status is not OK.  Check whether the status is retryable.
    if (!retry_policy->retryable_status_codes().Contains(*status)) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "%s: status %s not configured as retryable",
                DebugTag().c_str(), grpc_status_code_to_string(*status));
      }
      return false;
    }
  }
  // Record the failure and check whether retries are throttled.
  // Note that it's important for this check to come after the status
  // code check above, since we should only record failures whose statuses
  // match the configured retryable status codes, so that we don't count
  // things like failures due to malformed requests (INVALID_ARGUMENT).
  // Conversely, it's important for this to come before the remaining
  // checks, so that we don't fail to record failures due to other factors.
  if (retry_throttle_data != nullptr && !retry_throttle_data->RecordFailure()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "%s: retries throttled", DebugTag().c_str());
    }
    return false;
  }
  // Check whether the call is committed.
  if (calld->forwarder.committed()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "%s: retries already committed", DebugTag().c_str());
    }
    return false;
  }
  // Check whether we have retries remaining.
  ++calld->num_attempts_completed;
  if (calld->num_attempts_completed >= retry_policy->max_attempts()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
      gpr_log(GPR_INFO, "chand=%s: exceeded %d retry attempts",
              DebugTag().c_str(), retry_policy->max_attempts());
    }
    return false;
  }
  // Check server push-back.
  if (server_pushback.has_value()) {
    if (*server_pushback < Duration::Zero()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "%s: not retrying due to server push-back",
                DebugTag().c_str());
      }
      return false;
    } else {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_retry_trace)) {
        gpr_log(GPR_INFO, "%s: server push-back: retry in %" PRIu64 " ms",
                DebugTag().c_str(), server_pushback->millis());
      }
    }
  }
  // We should retry.
  return true;
}

absl::optional<Duration> RetryFilter::MaybeRetryDuration(
    CallState* call_state, CallAttemptState* call_attempt,
    const ServerMetadataHandle& md) {
  // TODO(ctiller): how to get lb drop detail here
  enum { kNoRetry, kTransparentRetry, kConfigurableRetry } retry = kNoRetry;
  // Handle transparent retries.
  const auto stream_network_state = md->get(GrpcStreamNetworkState());
  if (stream_network_state.has_value() && !call_state->forwarder.committed()) {
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
  if (retry == kNoRetry &&
      call_attempt->ShouldRetry(call_state, md->get(GrpcStatusMetadata()),
                                server_pushback)) {
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

auto RetryFilter::MakeCallAttempt(CallState* call_state,
                                  const ClientMetadataHandle& initial_metadata,
                                  Latch<grpc_polling_entity>* polling_entity) {
  auto* party = static_cast<Party*>(Activity::current());
  ++call_state->num_attempts_started;
  auto* attempt = new CallAttemptState(this, call_state->num_attempts_started);
  auto* arena = GetContext<Arena>();
  CallArgs child_call_args{
      arena->MakePooled<ClientMetadata>(arena),
      ClientInitialMetadataOutstandingToken::Empty(),  // what semantics
      polling_entity,
      &attempt->server_initial_metadata.sender,
      &attempt->client_to_server.receiver,
      &attempt->server_to_client.sender};
  *child_call_args.client_initial_metadata = initial_metadata->Copy();
  auto child_call = client_channel()->CreateLoadBalancedCallPromise(
      std::move(child_call_args), []() { Crash("on_commit not implemented"); },
      false);
  // If per_attempt_recv_timeout is set, start a timer.
  if (attempt->retry_policy != nullptr &&
      attempt->retry_policy->per_attempt_recv_timeout().has_value()) {
    party->Spawn(
        "per_attempt_recv_timeout",
        Sleep(Timestamp::Now() +
              attempt->retry_policy->per_attempt_recv_timeout().value()),
        [attempt](absl::Status) {
          if (!attempt->early_return.is_set()) {
            attempt->early_return.Set(ServerMetadataFromStatus(
                absl::CancelledError("retry perAttemptRecvTimeout exceeded")));
          }
        });
  }
  party->Spawn(
      "attempt_recv",
      Seq(attempt->server_initial_metadata.receiver.Next(),
          [call_state, attempt](NextResult<ServerMetadataHandle> md) mutable {
            const bool has_initial_metadata =
                md.has_value() &&
                !(*md)->get(GrpcTrailersOnly()).value_or(false);
            if (has_initial_metadata) {
              call_state->forwarder.Commit();
            }
            return If(
                has_initial_metadata,
                TrySeq(
                    Map(call_state->server_initial_metadata->Push(
                            std::move(*md)),
                        [](bool r) {
                          if (r) return absl::OkStatus();
                          return absl::CancelledError();
                        }),
                    ForEach(std::move(attempt->server_to_client.receiver),
                            [call_state](MessageHandle msg) {
                              return Map(
                                  call_state->server_to_client_messages->Push(
                                      std::move(msg)),
                                  [](bool r) {
                                    if (r) return absl::OkStatus();
                                    return absl::CancelledError();
                                  });
                            })),
                Immediate(absl::OkStatus()));
          }),
      [attempt](absl::Status status) {
        if (status.ok()) return;
        if (attempt->early_return.is_set()) return;
        attempt->early_return.Set(ServerMetadataFromStatus(status));
      });
  party->Spawn("attempt_send",
               ForEach(MessageForwarder::Listener(call_state->forwarder),
                       [attempt](MessageHandle msg) {
                         return Map(attempt->client_to_server.sender.Push(
                                        std::move(msg)),
                                    [](bool r) {
                                      if (r) return absl::OkStatus();
                                      return absl::CancelledError();
                                    });
                       }),
               [attempt](absl::Status status) {
                 if (status.ok()) return;
                 if (attempt->early_return.is_set()) return;
                 attempt->early_return.Set(ServerMetadataFromStatus(status));
               });
  return Seq(
      Race(attempt->early_return.Wait(), std::move(child_call)),
      [this, call_state, attempt](ServerMetadataHandle result) {
        auto maybe_retry_duration =
            MaybeRetryDuration(call_state, attempt, result);
        return If(
            maybe_retry_duration.has_value(),
            [maybe_retry_duration]() {
              return Seq(
                  Sleep(Timestamp::Now() + *maybe_retry_duration),
                  []() -> LoopCtl<ServerMetadataHandle> { return Continue(); });
            },
            [result = std::move(result)]() mutable
            -> LoopCtl<ServerMetadataHandle> { return std::move(result); });
      });
}

ArenaPromise<ServerMetadataHandle> RetryFilter::MakeCallPromise(
    CallArgs call_args) {
  auto* call_state = GetContext<Arena>()->ManagedNew<CallState>(
      call_args.server_initial_metadata, call_args.server_to_client_messages);
  return Loop([this, call_state,
               initial_metadata = std::move(call_args.client_initial_metadata),
               polling_entity = call_args.polling_entity]() mutable {
    return MakeCallAttempt(call_state, initial_metadata, polling_entity);
  });
}

const grpc_channel_filter RetryFilter::kVtable = {
    RetryFilter::LegacyCallData::StartTransportStreamOpBatch,
    [](grpc_channel_element* elem, CallArgs call_args, NextPromiseFactory) {
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
