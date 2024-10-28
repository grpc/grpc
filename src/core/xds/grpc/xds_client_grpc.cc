//
// Copyright 2022 gRPC authors.
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

#include "src/core/xds/grpc/xds_client_grpc.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "envoy/service/status/v3/csds.upb.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/env.h"
#include "src/core/util/load_file.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_transport_grpc.h"
#include "src/core/xds/xds_client/xds_api.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_channel_args.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "upb/base/string_view.h"

// If gRPC is built with -DGRPC_XDS_USER_AGENT_NAME_SUFFIX="...", that string
// will be appended to the user agent name reported to the xDS server.
#ifdef GRPC_XDS_USER_AGENT_NAME_SUFFIX
#define GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING \
  " " GRPC_XDS_USER_AGENT_NAME_SUFFIX
#else
#define GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING ""
#endif

// If gRPC is built with -DGRPC_XDS_USER_AGENT_VERSION_SUFFIX="...", that string
// will be appended to the user agent version reported to the xDS server.
#ifdef GRPC_XDS_USER_AGENT_VERSION_SUFFIX
#define GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING \
  " " GRPC_XDS_USER_AGENT_VERSION_SUFFIX
#else
#define GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING ""
#endif

namespace grpc_core {

namespace {

// Metric labels.
constexpr absl::string_view kMetricLabelXdsServer = "grpc.xds.server";
constexpr absl::string_view kMetricLabelXdsAuthority = "grpc.xds.authority";
constexpr absl::string_view kMetricLabelXdsResourceType =
    "grpc.xds.resource_type";
constexpr absl::string_view kMetricLabelXdsCacheState = "grpc.xds.cache_state";

const auto kMetricResourceUpdatesValid =
    GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.xds_client.resource_updates_valid",
        "EXPERIMENTAL.  A counter of resources received that were considered "
        "valid.  The counter will be incremented even for resources that "
        "have not changed.",
        "{resource}", false)
        .Labels(kMetricLabelTarget, kMetricLabelXdsServer,
                kMetricLabelXdsResourceType)
        .Build();

const auto kMetricResourceUpdatesInvalid =
    GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.xds_client.resource_updates_invalid",
        "EXPERIMENTAL.  A counter of resources received that were considered "
        "invalid.",
        "{resource}", false)
        .Labels(kMetricLabelTarget, kMetricLabelXdsServer,
                kMetricLabelXdsResourceType)
        .Build();

const auto kMetricServerFailure =
    GlobalInstrumentsRegistry::RegisterUInt64Counter(
        "grpc.xds_client.server_failure",
        "EXPERIMENTAL.  A counter of xDS servers going from healthy to "
        "unhealthy.  A server goes unhealthy when we have a connectivity "
        "failure or when the ADS stream fails without seeing a response "
        "message, as per gRFC A57.",
        "{failure}", false)
        .Labels(kMetricLabelTarget, kMetricLabelXdsServer)
        .Build();

const auto kMetricConnected =
    GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
        "grpc.xds_client.connected",
        "EXPERIMENTAL.  Whether or not the xDS client currently has a "
        "working ADS stream to the xDS server.  For a given server, this "
        "will be set to 0 when we have a connectivity failure or when the "
        "ADS stream fails without seeing a response message, as per gRFC "
        "A57.  It will be set to 1 when we receive the first response on "
        "an ADS stream.",
        "{bool}", false)
        .Labels(kMetricLabelTarget, kMetricLabelXdsServer)
        .Build();

const auto kMetricResources =
    GlobalInstrumentsRegistry::RegisterCallbackInt64Gauge(
        "grpc.xds_client.resources", "EXPERIMENTAL.  Number of xDS resources.",
        "{resource}", false)
        .Labels(kMetricLabelTarget, kMetricLabelXdsAuthority,
                kMetricLabelXdsResourceType, kMetricLabelXdsCacheState)
        .Build();

}  // namespace

//
// GrpcXdsClient::MetricsReporter
//

class GrpcXdsClient::MetricsReporter final : public XdsMetricsReporter {
 public:
  explicit MetricsReporter(GrpcXdsClient& xds_client)
      : xds_client_(xds_client) {}

  void ReportResourceUpdates(absl::string_view xds_server,
                             absl::string_view resource_type,
                             uint64_t num_valid_resources,
                             uint64_t num_invalid_resources) override {
    xds_client_.stats_plugin_group_.AddCounter(
        kMetricResourceUpdatesValid, num_valid_resources,
        {xds_client_.key_, xds_server, resource_type}, {});
    xds_client_.stats_plugin_group_.AddCounter(
        kMetricResourceUpdatesInvalid, num_invalid_resources,
        {xds_client_.key_, xds_server, resource_type}, {});
  }

  void ReportServerFailure(absl::string_view xds_server) override {
    xds_client_.stats_plugin_group_.AddCounter(
        kMetricServerFailure, 1, {xds_client_.key_, xds_server}, {});
  }

 private:
  GrpcXdsClient& xds_client_;
};

//
// GrpcXdsClient
//

constexpr absl::string_view GrpcXdsClient::kServerKey;

namespace {

Mutex* g_mu = new Mutex;
const grpc_channel_args* g_channel_args ABSL_GUARDED_BY(*g_mu) = nullptr;
// Key bytes live in clients so they outlive the entries in this map
NoDestruct<std::map<absl::string_view, GrpcXdsClient*>> g_xds_client_map
    ABSL_GUARDED_BY(*g_mu);
char* g_fallback_bootstrap_config ABSL_GUARDED_BY(*g_mu) = nullptr;

absl::StatusOr<std::string> GetBootstrapContents(const char* fallback_config) {
  // First, try GRPC_XDS_BOOTSTRAP env var.
  auto path = GetEnv("GRPC_XDS_BOOTSTRAP");
  if (path.has_value()) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "Got bootstrap file location from GRPC_XDS_BOOTSTRAP "
           "environment variable: "
        << *path;
    auto contents = LoadFile(*path, /*add_null_terminator=*/true);
    if (!contents.ok()) return contents.status();
    return std::string(contents->as_string_view());
  }
  // Next, try GRPC_XDS_BOOTSTRAP_CONFIG env var.
  auto env_config = GetEnv("GRPC_XDS_BOOTSTRAP_CONFIG");
  if (env_config.has_value()) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "Got bootstrap contents from GRPC_XDS_BOOTSTRAP_CONFIG "
        << "environment variable";
    return std::move(*env_config);
  }
  // Finally, try fallback config.
  if (fallback_config != nullptr) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "Got bootstrap contents from fallback config";
    return fallback_config;
  }
  // No bootstrap config found.
  return absl::FailedPreconditionError(
      "Environment variables GRPC_XDS_BOOTSTRAP or GRPC_XDS_BOOTSTRAP_CONFIG "
      "not defined");
}

GlobalStatsPluginRegistry::StatsPluginGroup
GetStatsPluginGroupForKeyAndChannelArgs(absl::string_view key,
                                        const ChannelArgs& channel_args) {
  if (key == GrpcXdsClient::kServerKey) {
    return GlobalStatsPluginRegistry::GetStatsPluginsForServer(channel_args);
  }
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config(
      channel_args);
  std::string authority =
      channel_args.GetOwnedString(GRPC_ARG_DEFAULT_AUTHORITY)
          .value_or(
              CoreConfiguration::Get().resolver_registry().GetDefaultAuthority(
                  key));
  experimental::StatsPluginChannelScope scope(key, authority, endpoint_config);
  return GlobalStatsPluginRegistry::GetStatsPluginsForChannel(scope);
}

}  // namespace

absl::StatusOr<RefCountedPtr<GrpcXdsClient>> GrpcXdsClient::GetOrCreate(
    absl::string_view key, const ChannelArgs& args, const char* reason) {
  // If getting bootstrap from channel args, create a local XdsClient
  // instance for the channel or server instead of using the global instance.
  absl::optional<absl::string_view> bootstrap_config = args.GetString(
      GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG);
  if (bootstrap_config.has_value()) {
    auto bootstrap = GrpcXdsBootstrap::Create(*bootstrap_config);
    if (!bootstrap.ok()) return bootstrap.status();
    grpc_channel_args* xds_channel_args = args.GetPointer<grpc_channel_args>(
        GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_CLIENT_CHANNEL_ARGS);
    auto channel_args = ChannelArgs::FromC(xds_channel_args);
    return MakeRefCounted<GrpcXdsClient>(
        key, std::move(*bootstrap), channel_args,
        MakeRefCounted<GrpcXdsTransportFactory>(channel_args),
        GetStatsPluginGroupForKeyAndChannelArgs(key, args));
  }
  // Otherwise, use the global instance.
  MutexLock lock(g_mu);
  auto it = g_xds_client_map->find(key);
  if (it != g_xds_client_map->end()) {
    auto xds_client = it->second->RefIfNonZero(DEBUG_LOCATION, reason);
    if (xds_client != nullptr) {
      return xds_client.TakeAsSubclass<GrpcXdsClient>();
    }
  }
  // Find bootstrap contents.
  auto bootstrap_contents = GetBootstrapContents(g_fallback_bootstrap_config);
  if (!bootstrap_contents.ok()) return bootstrap_contents.status();
  GRPC_TRACE_LOG(xds_client, INFO)
      << "xDS bootstrap contents: " << *bootstrap_contents;
  // Parse bootstrap.
  auto bootstrap = GrpcXdsBootstrap::Create(*bootstrap_contents);
  if (!bootstrap.ok()) return bootstrap.status();
  // Instantiate XdsClient.
  auto channel_args = ChannelArgs::FromC(g_channel_args);
  auto xds_client = MakeRefCounted<GrpcXdsClient>(
      key, std::move(*bootstrap), channel_args,
      MakeRefCounted<GrpcXdsTransportFactory>(channel_args),
      GetStatsPluginGroupForKeyAndChannelArgs(key, args));
  g_xds_client_map->emplace(xds_client->key(), xds_client.get());
  GRPC_TRACE_LOG(xds_client, INFO) << "[xds_client " << xds_client.get()
                                   << "] Created xDS client for key " << key;
  return xds_client;
}

namespace {

std::string UserAgentName() {
  return absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING,
                      GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING);
}

std::string UserAgentVersion() {
  return absl::StrCat("C-core ", grpc_version_string(),
                      GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING,
                      GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING);
}

}  // namespace

GrpcXdsClient::GrpcXdsClient(
    absl::string_view key, std::shared_ptr<GrpcXdsBootstrap> bootstrap,
    const ChannelArgs& args,
    RefCountedPtr<XdsTransportFactory> transport_factory,
    GlobalStatsPluginRegistry::StatsPluginGroup stats_plugin_group)
    : XdsClient(
          bootstrap, transport_factory,
          grpc_event_engine::experimental::GetDefaultEventEngine(),
          std::make_unique<MetricsReporter>(*this), UserAgentName(),
          UserAgentVersion(),
          std::max(Duration::Zero(),
                   args.GetDurationFromIntMillis(
                           GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS)
                       .value_or(Duration::Seconds(15)))),
      key_(key),
      certificate_provider_store_(MakeOrphanable<CertificateProviderStore>(
          static_cast<const GrpcXdsBootstrap&>(this->bootstrap())
              .certificate_providers())),
      stats_plugin_group_(std::move(stats_plugin_group)),
      registered_metric_callback_(stats_plugin_group_.RegisterCallback(
          [this](CallbackMetricReporter& reporter) {
            ReportCallbackMetrics(reporter);
          },
          Duration::Seconds(5), kMetricConnected, kMetricResources)),
      lrs_client_(MakeRefCounted<LrsClient>(
          std::move(bootstrap), UserAgentName(), UserAgentVersion(),
          std::move(transport_factory),
          grpc_event_engine::experimental::GetDefaultEventEngine())) {}

void GrpcXdsClient::Orphaned() {
  registered_metric_callback_.reset();
  XdsClient::Orphaned();
  lrs_client_.reset();
  MutexLock lock(g_mu);
  auto it = g_xds_client_map->find(key_);
  if (it != g_xds_client_map->end() && it->second == this) {
    g_xds_client_map->erase(it);
  }
}

void GrpcXdsClient::ResetBackoff() {
  XdsClient::ResetBackoff();
  lrs_client_->ResetBackoff();
}

grpc_pollset_set* GrpcXdsClient::interested_parties() const {
  return reinterpret_cast<GrpcXdsTransportFactory*>(transport_factory())
      ->interested_parties();
}

namespace {

std::vector<RefCountedPtr<GrpcXdsClient>> GetAllXdsClients() {
  MutexLock lock(g_mu);
  std::vector<RefCountedPtr<GrpcXdsClient>> xds_clients;
  for (const auto& key_client : *g_xds_client_map) {
    auto xds_client =
        key_client.second->RefIfNonZero(DEBUG_LOCATION, "DumpAllClientConfigs");
    if (xds_client != nullptr) {
      xds_clients.emplace_back(xds_client.TakeAsSubclass<GrpcXdsClient>());
    }
  }
  return xds_clients;
}

}  // namespace

// ABSL_NO_THREAD_SAFETY_ANALYSIS because we have to manually manage locks for
// individual XdsClients and compiler struggles with checking the validity
grpc_slice GrpcXdsClient::DumpAllClientConfigs()
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  auto xds_clients = GetAllXdsClients();
  upb::Arena arena;
  // Contains strings that should survive till serialization
  std::set<std::string> string_pool;
  auto response = envoy_service_status_v3_ClientStatusResponse_new(arena.ptr());
  // We lock each XdsClient mutex till we are done with the serialization to
  // ensure that all data referenced from the UPB proto message stays alive.
  for (const auto& xds_client : xds_clients) {
    auto client_config =
        envoy_service_status_v3_ClientStatusResponse_add_config(response,
                                                                arena.ptr());
    xds_client->mu()->Lock();
    xds_client->DumpClientConfig(&string_pool, arena.ptr(), client_config);
    envoy_service_status_v3_ClientConfig_set_client_scope(
        client_config, StdStringToUpbString(xds_client->key()));
  }
  // Serialize the upb message to bytes
  size_t output_length;
  char* output = envoy_service_status_v3_ClientStatusResponse_serialize(
      response, arena.ptr(), &output_length);
  for (const auto& xds_client : xds_clients) {
    xds_client->mu()->Unlock();
  }
  return grpc_slice_from_cpp_string(std::string(output, output_length));
}

void GrpcXdsClient::ReportCallbackMetrics(CallbackMetricReporter& reporter) {
  MutexLock lock(mu());
  ReportResourceCounts([&](const ResourceCountLabels& labels, uint64_t count) {
    reporter.Report(
        kMetricResources, count,
        {key_, labels.xds_authority, labels.resource_type, labels.cache_state},
        {});
  });
  ReportServerConnections([&](absl::string_view xds_server, bool connected) {
    reporter.Report(kMetricConnected, connected, {key_, xds_server}, {});
  });
}

namespace internal {

void SetXdsChannelArgsForTest(grpc_channel_args* args) {
  MutexLock lock(g_mu);
  g_channel_args = args;
}

void UnsetGlobalXdsClientsForTest() {
  MutexLock lock(g_mu);
  g_xds_client_map->clear();
}

void SetXdsFallbackBootstrapConfig(const char* config) {
  MutexLock lock(g_mu);
  gpr_free(g_fallback_bootstrap_config);
  g_fallback_bootstrap_config = gpr_strdup(config);
}

}  // namespace internal

}  // namespace grpc_core

// The returned bytes may contain NULL(0), so we can't use c-string.
grpc_slice grpc_dump_xds_configs(void) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  return grpc_core::GrpcXdsClient::DumpAllClientConfigs();
}
