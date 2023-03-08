//
//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H
#define GRPC_SRC_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "opencensus/tags/tag_key.h"
#include "opencensus/tags/tag_map.h"

#include <grpc/event_engine/event_engine.h>
#include <grpcpp/opencensus.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc {

// The following using declarations have been added to prevent breaking users
// that were directly using this header file.
using experimental::ClientMethodTagKey;  // NOLINT
using experimental::ClientStatusTagKey;  // NOLINT
using experimental::ServerMethodTagKey;  // NOLINT
using experimental::ServerStatusTagKey;  // NOLINT

using experimental::kRpcClientReceivedBytesPerRpcMeasureName;        // NOLINT
using experimental::kRpcClientReceivedMessagesPerRpcMeasureName;     // NOLINT
using experimental::kRpcClientRetriesPerCallMeasureName;             // NOLINT
using experimental::kRpcClientRetryDelayPerCallMeasureName;          // NOLINT
using experimental::kRpcClientRoundtripLatencyMeasureName;           // NOLINT
using experimental::kRpcClientSentBytesPerRpcMeasureName;            // NOLINT
using experimental::kRpcClientSentMessagesPerRpcMeasureName;         // NOLINT
using experimental::kRpcClientServerLatencyMeasureName;              // NOLINT
using experimental::kRpcClientStartedRpcsMeasureName;                // NOLINT
using experimental::kRpcClientTransparentRetriesPerCallMeasureName;  // NOLINT

using experimental::kRpcServerReceivedBytesPerRpcMeasureName;     // NOLINT
using experimental::kRpcServerReceivedMessagesPerRpcMeasureName;  // NOLINT
using experimental::kRpcServerSentBytesPerRpcMeasureName;         // NOLINT
using experimental::kRpcServerSentMessagesPerRpcMeasureName;      // NOLINT
using experimental::kRpcServerServerLatencyMeasureName;           // NOLINT
using experimental::kRpcServerStartedRpcsMeasureName;             // NOLINT

using experimental::ClientCompletedRpcsCumulative;              // NOLINT
using experimental::ClientReceivedBytesPerRpcCumulative;        // NOLINT
using experimental::ClientReceivedMessagesPerRpcCumulative;     // NOLINT
using experimental::ClientRetriesCumulative;                    // NOLINT
using experimental::ClientRetriesPerCallCumulative;             // NOLINT
using experimental::ClientRetryDelayPerCallCumulative;          // NOLINT
using experimental::ClientRoundtripLatencyCumulative;           // NOLINT
using experimental::ClientSentBytesPerRpcCumulative;            // NOLINT
using experimental::ClientSentMessagesPerRpcCumulative;         // NOLINT
using experimental::ClientServerLatencyCumulative;              // NOLINT
using experimental::ClientStartedRpcsCumulative;                // NOLINT
using experimental::ClientTransparentRetriesCumulative;         // NOLINT
using experimental::ClientTransparentRetriesPerCallCumulative;  // NOLINT

using experimental::ServerCompletedRpcsCumulative;           // NOLINT
using experimental::ServerReceivedBytesPerRpcCumulative;     // NOLINT
using experimental::ServerReceivedMessagesPerRpcCumulative;  // NOLINT
using experimental::ServerSentBytesPerRpcCumulative;         // NOLINT
using experimental::ServerSentMessagesPerRpcCumulative;      // NOLINT
using experimental::ServerServerLatencyCumulative;           // NOLINT
using experimental::ServerStartedRpcsCumulative;             // NOLINT

using experimental::ClientCompletedRpcsMinute;              // NOLINT
using experimental::ClientReceivedBytesPerRpcMinute;        // NOLINT
using experimental::ClientReceivedMessagesPerRpcMinute;     // NOLINT
using experimental::ClientRetriesMinute;                    // NOLINT
using experimental::ClientRetriesPerCallMinute;             // NOLINT
using experimental::ClientRetryDelayPerCallMinute;          // NOLINT
using experimental::ClientRoundtripLatencyMinute;           // NOLINT
using experimental::ClientSentBytesPerRpcMinute;            // NOLINT
using experimental::ClientSentMessagesPerRpcMinute;         // NOLINT
using experimental::ClientServerLatencyMinute;              // NOLINT
using experimental::ClientStartedRpcsMinute;                // NOLINT
using experimental::ClientTransparentRetriesMinute;         // NOLINT
using experimental::ClientTransparentRetriesPerCallMinute;  // NOLINT

using experimental::ServerCompletedRpcsMinute;           // NOLINT
using experimental::ServerReceivedBytesPerRpcMinute;     // NOLINT
using experimental::ServerReceivedMessagesPerRpcMinute;  // NOLINT
using experimental::ServerSentBytesPerRpcMinute;         // NOLINT
using experimental::ServerSentMessagesPerRpcMinute;      // NOLINT
using experimental::ServerServerLatencyMinute;           // NOLINT
using experimental::ServerStartedRpcsMinute;             // NOLINT

using experimental::ClientCompletedRpcsHour;              // NOLINT
using experimental::ClientReceivedBytesPerRpcHour;        // NOLINT
using experimental::ClientReceivedMessagesPerRpcHour;     // NOLINT
using experimental::ClientRetriesHour;                    // NOLINT
using experimental::ClientRetriesPerCallHour;             // NOLINT
using experimental::ClientRetryDelayPerCallHour;          // NOLINT
using experimental::ClientRoundtripLatencyHour;           // NOLINT
using experimental::ClientSentBytesPerRpcHour;            // NOLINT
using experimental::ClientSentMessagesPerRpcHour;         // NOLINT
using experimental::ClientServerLatencyHour;              // NOLINT
using experimental::ClientStartedRpcsHour;                // NOLINT
using experimental::ClientTransparentRetriesHour;         // NOLINT
using experimental::ClientTransparentRetriesPerCallHour;  // NOLINT

using experimental::ServerCompletedRpcsHour;           // NOLINT
using experimental::ServerReceivedBytesPerRpcHour;     // NOLINT
using experimental::ServerReceivedMessagesPerRpcHour;  // NOLINT
using experimental::ServerSentBytesPerRpcHour;         // NOLINT
using experimental::ServerSentMessagesPerRpcHour;      // NOLINT
using experimental::ServerServerLatencyHour;           // NOLINT
using experimental::ServerStartedRpcsHour;             // NOLINT

namespace internal {

// Enables/Disables OpenCensus stats/tracing. It's only safe to do at the start
// of a program, before any channels/servers are built.
void EnableOpenCensusStats(bool enable);
void EnableOpenCensusTracing(bool enable);
// Gets the current status of OpenCensus stats/tracing
bool OpenCensusStatsEnabled();
bool OpenCensusTracingEnabled();

// Registers various things for the OpenCensus plugin.
class OpenCensusRegistry {
 public:
  struct Label {
    std::string key;
    opencensus::tags::TagKey tag_key;
    std::string value;
  };

  struct Attribute {
    std::string key;
    std::string value;
  };

  static OpenCensusRegistry& Get();

  // Registers the functions to be run post-init.
  void RegisterFunctions(std::function<void()> f) {
    exporter_registry_.push_back(std::move(f));
  }

  void RegisterWaitOnReady() { wait_on_ready_ = true; }

  // Runs the registry post-init exactly once. Protected with an absl::CallOnce.
  void RunFunctionsPostInit() {
    absl::call_once(once_, &OpenCensusRegistry::RunFunctionsPostInitHelper,
                    this);
  }

  void RegisterConstantLabels(
      const std::map<std::string, std::string>& labels) {
    grpc_core::MutexLock lock(&mu_);
    constant_labels_.reserve(labels.size());
    for (const auto& label : labels) {
      auto tag_key = opencensus::tags::TagKey::Register(label.first);
      constant_labels_.emplace_back(Label{label.first, tag_key, label.second});
    }
  }

  void RegisterConstantAttributes(std::vector<Attribute> attributes) {
    grpc_core::MutexLock lock(&mu_);
    constant_attributes_ = std::move(attributes);
  }

  void NotifyOnReady(absl::AnyInvocable<void()> callback) {
    grpc_core::MutexLock lock(&mu_);
    // Environment has already been detected
    if (ready_) {
      // Execute on the event engine to avoid deadlocks.
      return event_engine()->Run(std::move(callback));
    }
    callbacks_.push_back(std::move(callback));
  }

  ::opencensus::tags::TagMap PopulateTagMapWithConstantLabels(
      const ::opencensus::tags::TagMap& tag_map);

  void PopulateCensusContextWithConstantAttributes(
      grpc::experimental::CensusContext* context);

  void SetReady() {
    std::vector<absl::AnyInvocable<void()>> callbacks;
    {
      grpc_core::MutexLock lock(&mu_);
      ready_ = true;
      callbacks = std::move(callbacks_);
    }
    for (auto& callback : callbacks) {
      callback();
    }
  }

  bool Ready() {
    if (!wait_on_ready_) {
      return true;
    }
    grpc_core::MutexLock lock(&mu_);
    return ready_;
  }

  const std::vector<Label>& ConstantLabels() {
    grpc_core::MutexLock lock(&mu_);
    return constant_labels_;
  }

  const std::vector<Attribute>& ConstantAttributes() {
    grpc_core::MutexLock lock(&mu_);
    return constant_attributes_;
  }

 private:
  void RunFunctionsPostInitHelper() {
    for (const auto& f : exporter_registry_) {
      f();
    }
  }

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (event_engine_ == nullptr) {
      event_engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
    }
    return event_engine_;
  }

  OpenCensusRegistry() = default;

  std::vector<std::function<void()>> exporter_registry_;
  absl::once_flag once_;
  // Some setups might need to set up the constant labels that are fetched after
  // start up. wait_on_ready_ allows implementations to check whether there is
  // such a need. This is only set before grpc_init in a single thread, so it
  // should not need any protection.
  bool wait_on_ready_ = false;
  grpc_core::Mutex mu_;
  // If wait_on_ready_ is true, ready_ indicates whether the plugin is now ready
  // to start serving.
  bool ready_ ABSL_GUARDED_BY(mu_) = false;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_
      ABSL_GUARDED_BY(mu_);
  std::vector<Label> constant_labels_ ABSL_GUARDED_BY(mu_);
  std::vector<Attribute> constant_attributes_ ABSL_GUARDED_BY(mu_);
  std::vector<absl::AnyInvocable<void()>> callbacks_ ABSL_GUARDED_BY(mu_);
};

}  // namespace internal

}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_FILTERS_CENSUS_GRPC_PLUGIN_H
