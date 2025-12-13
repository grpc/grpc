//
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
//

#include "src/core/ext/filters/fault_injection/fault_injection_filter.h"

#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/fault_injection/fault_injection_service_config_parser.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/util/time.h"
#include "absl/log/log.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

bool FaultInjectionFilter::Config::Equals(const FilterConfig& other) const {
  const auto& o = DownCast<const Config&>(other);
  return abort_code == o.abort_code && abort_message == o.abort_message &&
         abort_code_header == o.abort_code_header &&
         abort_percentage_header == o.abort_percentage_header &&
         delay == o.delay && delay_header == o.delay_header &&
         delay_percentage_header == o.delay_percentage_header &&
         delay_percentage_numerator == o.delay_percentage_numerator &&
         delay_percentage_denominator == o.delay_percentage_denominator &&
         max_faults == o.max_faults;
}

std::string FaultInjectionFilter::Config::ToString() const {
  std::vector<std::string> parts;
  if (abort_code != GRPC_STATUS_OK || !abort_code_header.empty()) {
    if (abort_code != GRPC_STATUS_OK) {
      parts.push_back(
          absl::StrCat("abort_code=", grpc_status_code_to_string(abort_code)));
    }
    if (!abort_code_header.empty()) {
      parts.push_back(
          absl::StrCat("abort_code_header=\"", abort_code_header, "\""));
    }
    parts.push_back(absl::StrCat("abort_message=\"", abort_message, "\""));
    if (!abort_percentage_header.empty()) {
      parts.push_back(absl::StrCat("abort_percentage_header=\"",
                                   abort_percentage_header, "\""));
    }
    if (abort_percentage_numerator > 0) {
      parts.push_back(absl::StrCat("abort_percentage_numerator=",
                                   abort_percentage_numerator));
      parts.push_back(absl::StrCat("abort_percentage_denominator=",
                                   abort_percentage_denominator));
    }
  }
  if (delay != Duration::Zero() || !delay_header.empty()) {
    if (delay != Duration::Zero()) {
      parts.push_back(absl::StrCat("delay=", delay.ToString()));
    }
    if (!delay_header.empty()) {
      parts.push_back(absl::StrCat("delay_header=\"", delay_header, "\""));
    }
    if (!delay_percentage_header.empty()) {
      parts.push_back(absl::StrCat("delay_percentage_header=\"",
                                   delay_percentage_header, "\""));
    }
    if (delay_percentage_numerator > 0) {
      parts.push_back(absl::StrCat("delay_percentage_numerator=",
                                   delay_percentage_numerator));
      parts.push_back(absl::StrCat("delay_percentage_denominator=",
                                   delay_percentage_denominator));
    }
  }
  parts.push_back(absl::StrCat("max_faults=", max_faults));
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

namespace {

std::atomic<uint32_t> g_active_faults{0};
static_assert(
    std::is_trivially_destructible<std::atomic<uint32_t>>::value,
    "the active fault counter needs to have a trivially destructible type");

template <typename T>
auto AsInt(absl::string_view s) -> std::optional<T> {
  T x;
  if (absl::SimpleAtoi(s, &x)) return x;
  return std::nullopt;
}

inline bool UnderFraction(absl::InsecureBitGen* rand_generator,
                          const uint32_t numerator,
                          const uint32_t denominator) {
  if (numerator <= 0) return false;
  if (numerator >= denominator) return true;
  // Generate a random number in [0, denominator).
  const uint32_t random_number =
      absl::Uniform(absl::IntervalClosedOpen, *rand_generator, 0u, denominator);
  return random_number < numerator;
}

// Tracks an active faults lifetime.
// Increments g_active_faults when created, and decrements it when destroyed.
class FaultHandle {
 public:
  explicit FaultHandle(bool active) : active_(active) {
    if (active) {
      g_active_faults.fetch_add(1, std::memory_order_relaxed);
    }
  }
  ~FaultHandle() {
    if (active_) {
      g_active_faults.fetch_sub(1, std::memory_order_relaxed);
    }
  }
  FaultHandle(const FaultHandle&) = delete;
  FaultHandle& operator=(const FaultHandle&) = delete;
  FaultHandle(FaultHandle&& other) noexcept
      : active_(std::exchange(other.active_, false)) {}
  FaultHandle& operator=(FaultHandle&& other) noexcept {
    std::swap(active_, other.active_);
    return *this;
  }

 private:
  bool active_;
};

}  // namespace

class FaultInjectionFilter::InjectionDecision {
 public:
  InjectionDecision(uint32_t max_faults, Duration delay_time,
                    std::optional<absl::Status> abort_request)
      : max_faults_(max_faults),
        delay_time_(delay_time),
        abort_request_(abort_request) {}

  std::string ToString() const;
  Timestamp DelayUntil();
  absl::Status MaybeAbort() const;

 private:
  bool HaveActiveFaultsQuota() const;

  uint32_t max_faults_;
  Duration delay_time_;
  std::optional<absl::Status> abort_request_;
  FaultHandle active_fault_{false};
};

absl::StatusOr<std::unique_ptr<FaultInjectionFilter>>
FaultInjectionFilter::Create(const ChannelArgs&,
                             ChannelFilter::Args filter_args) {
  if (IsXdsChannelFilterChainPerRouteEnabled()) {
    if (filter_args.config() == nullptr) {
      return absl::InternalError("no config passed to fault injection filter");
    }
    if (filter_args.config()->type() != Config::Type()) {
      return absl::InternalError(
          absl::StrCat("wrong config type passed to fault injection filter: ",
                       filter_args.config()->type().name()));
    }
  }
  return std::make_unique<FaultInjectionFilter>(filter_args);
}

FaultInjectionFilter::FaultInjectionFilter(ChannelFilter::Args filter_args)
    : index_(filter_args.instance_id()),
      service_config_parser_index_(
          FaultInjectionServiceConfigParser::ParserIndex()),
      config_(filter_args.config().TakeAsSubclass<const Config>()) {}

// Construct a promise for one call.
ArenaPromise<absl::Status> FaultInjectionFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, FaultInjectionFilter* filter) {
  auto decision = filter->MakeInjectionDecision(md);
  GRPC_TRACE_LOG(fault_injection_filter, INFO)
      << "chand=" << this << ": Fault injection triggered "
      << decision.ToString();
  auto delay = decision.DelayUntil();
  return TrySeq(Sleep(delay), [decision = std::move(decision)]() {
    return decision.MaybeAbort();
  });
}

FaultInjectionFilter::InjectionDecision
FaultInjectionFilter::MakeInjectionDecision(
    const ClientMetadata& initial_metadata) {
  if (!IsXdsChannelFilterChainPerRouteEnabled()) {
    // Fetch the fault injection policy from the service config, based on the
    // relative index for which policy should this CallData use.
    auto* service_config_call_data = GetContext<ServiceConfigCallData>();
    auto* method_params = static_cast<FaultInjectionMethodParsedConfig*>(
        service_config_call_data->GetMethodParsedConfig(
            service_config_parser_index_));
    const FaultInjectionMethodParsedConfig::FaultInjectionPolicy* fi_policy =
        nullptr;
    if (method_params != nullptr) {
      fi_policy = method_params->fault_injection_policy(index_);
    }
    // Shouldn't ever be null, but just in case, return a no-op decision.
    if (fi_policy == nullptr) {
      return InjectionDecision(/*max_faults=*/0,
                               /*delay_time=*/Duration::Zero(),
                               /*abort_request=*/std::nullopt);
    }
    return MakeInjectionDecision(initial_metadata, *fi_policy);
  }
  // Shouldn't ever be null, but just in case, return a no-op decision.
  if (config_ == nullptr) {
    return InjectionDecision(/*max_faults=*/0, /*delay_time=*/Duration::Zero(),
                             /*abort_request=*/std::nullopt);
  }
  return MakeInjectionDecision(initial_metadata, *config_);
}

template <typename T>
FaultInjectionFilter::InjectionDecision
FaultInjectionFilter::MakeInjectionDecision(
    const ClientMetadata& initial_metadata, const T& config) {
  grpc_status_code abort_code = config.abort_code;
  uint32_t abort_percentage_numerator = config.abort_percentage_numerator;
  uint32_t delay_percentage_numerator = config.delay_percentage_numerator;
  Duration delay = config.delay;

  // Update the policy with values in initial metadata.
  if (!config.abort_code_header.empty() ||
      !config.abort_percentage_header.empty() || !config.delay_header.empty() ||
      !config.delay_percentage_header.empty()) {
    std::string buffer;
    if (!config.abort_code_header.empty() && abort_code == GRPC_STATUS_OK) {
      auto value =
          initial_metadata.GetStringValue(config.abort_code_header, &buffer);
      if (value.has_value()) {
        grpc_status_code_from_int(
            AsInt<int>(*value).value_or(GRPC_STATUS_UNKNOWN), &abort_code);
      }
    }
    if (!config.abort_percentage_header.empty()) {
      auto value = initial_metadata.GetStringValue(
          config.abort_percentage_header, &buffer);
      if (value.has_value()) {
        abort_percentage_numerator = std::min(
            AsInt<uint32_t>(*value).value_or(-1), abort_percentage_numerator);
      }
    }
    if (!config.delay_header.empty() && delay == Duration::Zero()) {
      auto value =
          initial_metadata.GetStringValue(config.delay_header, &buffer);
      if (value.has_value()) {
        delay = Duration::Milliseconds(
            std::max(AsInt<int64_t>(*value).value_or(0), int64_t{0}));
      }
    }
    if (!config.delay_percentage_header.empty()) {
      auto value = initial_metadata.GetStringValue(
          config.delay_percentage_header, &buffer);
      if (value.has_value()) {
        delay_percentage_numerator = std::min(
            AsInt<uint32_t>(*value).value_or(-1), delay_percentage_numerator);
      }
    }
  }
  // Roll the dice
  bool delay_request = delay != Duration::Zero();
  bool abort_request = abort_code != GRPC_STATUS_OK;
  if (delay_request || abort_request) {
    MutexLock lock(&mu_);
    if (delay_request) {
      delay_request =
          UnderFraction(&delay_rand_generator_, delay_percentage_numerator,
                        config.delay_percentage_denominator);
    }
    if (abort_request) {
      abort_request =
          UnderFraction(&abort_rand_generator_, abort_percentage_numerator,
                        config.abort_percentage_denominator);
    }
  }

  return InjectionDecision(
      config.max_faults, delay_request ? delay : Duration::Zero(),
      abort_request ? std::optional<absl::Status>(absl::Status(
                          static_cast<absl::StatusCode>(abort_code),
                          config.abort_message))
                    : std::nullopt);
}

bool FaultInjectionFilter::InjectionDecision::HaveActiveFaultsQuota() const {
  return g_active_faults.load(std::memory_order_acquire) < max_faults_;
}

Timestamp FaultInjectionFilter::InjectionDecision::DelayUntil() {
  if (delay_time_ != Duration::Zero() && HaveActiveFaultsQuota()) {
    active_fault_ = FaultHandle{true};
    return Timestamp::Now() + delay_time_;
  }
  return Timestamp::InfPast();
}

absl::Status FaultInjectionFilter::InjectionDecision::MaybeAbort() const {
  if (abort_request_.has_value() &&
      (delay_time_ != Duration::Zero() || HaveActiveFaultsQuota())) {
    return abort_request_.value();
  }
  return absl::OkStatus();
}

std::string FaultInjectionFilter::InjectionDecision::ToString() const {
  return absl::StrCat("delay=", delay_time_ != Duration::Zero(),
                      " abort=", abort_request_.has_value());
}

const grpc_channel_filter FaultInjectionFilter::kFilterVtable =
    MakePromiseBasedFilter<FaultInjectionFilter, FilterEndpoint::kClient>();

void FaultInjectionFilterRegister(CoreConfiguration::Builder* builder) {
  FaultInjectionServiceConfigParser::Register(builder);
}

}  // namespace grpc_core
