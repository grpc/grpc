/*
 *
 * Copyright 2020 The gRPC Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_UTILS_CC
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_UTILS_CC

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/fault_injection.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/transport/metadata_batch.h"

#define X_ENVOY_FAULT_ABORT_GRPC_REQUEST "x-envoy-fault-abort-grpc-request"
#define X_ENVOY_FAULT_ABORT_REQUEST_PERCENTAGE "x-envoy-fault-abort-percentage"
#define X_ENVOY_FAULT_DELAY_REQUEST "x-envoy-fault-delay-request"
#define X_ENVOY_FAULT_DELAY_REQUEST_PERCENTAGE \
  "x-envoy-fault-delay-request-percentage"
#define X_ENVOY_FAULT_THROUGHPUT_RESPONSE "x-envoy-fault-throughput-response"
#define X_ENVOY_FAULT_THROUGHPUT_RESPONSE_PERCENTAGE \
  "x-envoy-fault-throughput-response-percentage"

using grpc_core::internal::ClientChannelMethodParsedConfig;

namespace {

uint32_t g_active_faults = 0;
grpc_core::Mutex g_mu_active_faults;

inline int GetLinkedMetadatumValueInt(grpc_linked_mdelem* md) {
  return gpr_parse_nonnegative_int(
      grpc_slice_to_c_string(GRPC_MDVALUE(md->md)));
}

bool UnderFraction(const uint32_t fraction_per_million) {
  // Generate a random number in [0, 1000000).
  const uint32_t random_number = rand() % 1000000;
  return random_number < fraction_per_million;
}

}  // namespace

namespace grpc_core {
namespace internal {

FaultInjectionData::FaultInjectionData() {
  fault_injected_ = false;
  injection_finished_ = false;
}

FaultInjectionData::~FaultInjectionData() {
  // If this RPC is fault injected but the injection wasn't finished, we need
  // to correct the active faults counting.
  if (fault_injected_ && !injection_finished_) {
    // FaultInjectionData is owned by CallData. When RPC finished, the
    // FaultInjectionData will be deallocated at the same time.
    MutexLock lock(&g_mu_active_faults);
    injection_finished_ = true;
    g_active_faults--;
  }
}

void FaultInjectionData::UpdateByMetadata(
    grpc_metadata_batch* initial_metadata) {
  for (grpc_linked_mdelem* md = initial_metadata->list.head; md != nullptr;
       md = md->next) {
    absl::string_view key = StringViewFromSlice(GRPC_MDKEY(md->md));

    if (key == X_ENVOY_FAULT_ABORT_GRPC_REQUEST) {
      if (!fault_injection_policy_.abort_by_headers) break;
      int candidate = GetLinkedMetadatumValueInt(md);
      if (candidate < 0 || candidate > 15)
        candidate = 2;  // GRPC_STATUS_UNKNOWN
      fault_injection_policy_.abort_code =
          static_cast<grpc_status_code>(candidate);
    } else if (key == X_ENVOY_FAULT_ABORT_REQUEST_PERCENTAGE) {
      if (!fault_injection_policy_.abort_by_headers) break;
      // TODO(lidiz) Once the processing of HTTP filters is settled. Use the
      // denominator in HTTPFaultInject, instead of 1000000.
      fault_injection_policy_.abort_per_million =
          GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
    } else if (key == X_ENVOY_FAULT_DELAY_REQUEST) {
      if (!fault_injection_policy_.delay_by_headers) break;
      fault_injection_policy_.delay =
          static_cast<grpc_millis> GPR_MAX(GetLinkedMetadatumValueInt(md), 0);
    } else if (key == X_ENVOY_FAULT_DELAY_REQUEST_PERCENTAGE) {
      if (!fault_injection_policy_.delay_by_headers) break;
      // TODO(lidiz) ditto.
      fault_injection_policy_.delay_per_million =
          GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
    } else if (key == X_ENVOY_FAULT_THROUGHPUT_RESPONSE) {
      if (!fault_injection_policy_.rate_limit_by_headers) break;
      fault_injection_policy_.per_stream_response_rate_limit =
          GPR_MAX(GetLinkedMetadatumValueInt(md), 0);
    } else if (key == X_ENVOY_FAULT_THROUGHPUT_RESPONSE_PERCENTAGE) {
      if (!fault_injection_policy_.rate_limit_by_headers) break;
      // TODO(lidiz) ditto.
      fault_injection_policy_.response_rate_limit_per_million =
          GPR_CLAMP(GetLinkedMetadatumValueInt(md), 0, 1000000);
    }
  }
}

grpc_error* FaultInjectionData::MaybeAbort() {
  if (fault_injected_ || fault_injection_policy_.abort_code == GRPC_STATUS_OK) {
    return GRPC_ERROR_NONE;
  }
  if (UnderFraction(fault_injection_policy_.abort_per_million)) {
    if (BeginFaultInjection()) {
      return grpc_error_set_int(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              fault_injection_policy_.abort_message.c_str()),
          GRPC_ERROR_INT_GRPC_STATUS, fault_injection_policy_.abort_code);
    }
  }
  return GRPC_ERROR_NONE;
}

bool FaultInjectionData::BeginFaultInjection() {
  MutexLock lock(&g_mu_active_faults);
  if (g_active_faults >= fault_injection_policy_.max_faults) return false;
  // Increase active fault
  fault_injected_ = true;
  g_active_faults++;
  return true;
}

bool FaultInjectionData::EndFaultInjection() {
  if (injection_finished_) return false;
  // The fault injection is finished, e.g., delay finished.
  injection_finished_ = true;
  {
    MutexLock lock(&g_mu_active_faults);
    g_active_faults--;
    return true;
  }
}

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_FAULT_INJECTION_UTILS_CC */
