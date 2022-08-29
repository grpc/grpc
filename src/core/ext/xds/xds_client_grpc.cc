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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_client_grpc.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/xds/certificate_provider_factory.h"
#include "src/core/ext/xds/certificate_provider_registry.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_cluster_specifier_plugin.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/ext/xds/xds_transport.h"
#include "src/core/ext/xds/xds_transport_grpc.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

//
// GrpcXdsClient
//

namespace {

Mutex* g_mu = nullptr;
const grpc_channel_args* g_channel_args ABSL_GUARDED_BY(*g_mu) = nullptr;
GrpcXdsClient* g_xds_client ABSL_GUARDED_BY(*g_mu) = nullptr;
char* g_fallback_bootstrap_config ABSL_GUARDED_BY(*g_mu) = nullptr;

}  // namespace

void XdsClientGlobalInit() {
  g_mu = new Mutex;
  XdsHttpFilterRegistry::Init();
  XdsClusterSpecifierPluginRegistry::Init();
}

// TODO(roth): Find a better way to clear the fallback config that does
// not require using ABSL_NO_THREAD_SAFETY_ANALYSIS.
void XdsClientGlobalShutdown() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  gpr_free(g_fallback_bootstrap_config);
  g_fallback_bootstrap_config = nullptr;
  delete g_mu;
  g_mu = nullptr;
  XdsHttpFilterRegistry::Shutdown();
  XdsClusterSpecifierPluginRegistry::Shutdown();
}

namespace {

absl::StatusOr<std::string> GetBootstrapContents(const char* fallback_config) {
  // First, try GRPC_XDS_BOOTSTRAP env var.
  UniquePtr<char> path(gpr_getenv("GRPC_XDS_BOOTSTRAP"));
  if (path != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "Got bootstrap file location from GRPC_XDS_BOOTSTRAP "
              "environment variable: %s",
              path.get());
    }
    grpc_slice contents;
    grpc_error_handle error =
        grpc_load_file(path.get(), /*add_null_terminator=*/true, &contents);
    if (!GRPC_ERROR_IS_NONE(error)) return grpc_error_to_absl_status(error);
    std::string contents_str(StringViewFromSlice(contents));
    grpc_slice_unref_internal(contents);
    return contents_str;
  }
  // Next, try GRPC_XDS_BOOTSTRAP_CONFIG env var.
  UniquePtr<char> env_config(gpr_getenv("GRPC_XDS_BOOTSTRAP_CONFIG"));
  if (env_config != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "Got bootstrap contents from GRPC_XDS_BOOTSTRAP_CONFIG "
              "environment variable");
    }
    return env_config.get();
  }
  // Finally, try fallback config.
  if (fallback_config != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "Got bootstrap contents from fallback config");
    }
    return fallback_config;
  }
  // No bootstrap config found.
  return absl::FailedPreconditionError(
      "Environment variables GRPC_XDS_BOOTSTRAP or GRPC_XDS_BOOTSTRAP_CONFIG "
      "not defined");
}

}  // namespace

absl::StatusOr<RefCountedPtr<GrpcXdsClient>> GrpcXdsClient::GetOrCreate(
    const ChannelArgs& args, const char* reason) {
  // Construct certificate provider plugin map.
  auto certificate_provider_plugin_map =
      absl::make_unique<GrpcXdsCertificateProviderPluginMap>();
  // If getting bootstrap from channel args, create a local XdsClient
  // instance for the channel or server instead of using the global instance.
  absl::optional<absl::string_view> bootstrap_config = args.GetString(
      GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG);
  if (bootstrap_config.has_value()) {
    grpc_error_handle error = GRPC_ERROR_NONE;
    std::unique_ptr<XdsBootstrap> bootstrap = XdsBootstrap::Create(
        *bootstrap_config, std::move(certificate_provider_plugin_map), &error);
    if (!GRPC_ERROR_IS_NONE(error)) return grpc_error_to_absl_status(error);
    grpc_channel_args* xds_channel_args = args.GetPointer<grpc_channel_args>(
        GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_CLIENT_CHANNEL_ARGS);
    return MakeRefCounted<GrpcXdsClient>(std::move(bootstrap),
                                         ChannelArgs::FromC(xds_channel_args));
  }
  // Otherwise, use the global instance.
  MutexLock lock(g_mu);
  if (g_xds_client != nullptr) {
    auto xds_client = g_xds_client->RefIfNonZero(DEBUG_LOCATION, reason);
    if (xds_client != nullptr) return xds_client;
  }
  // Find bootstrap contents.
  auto bootstrap_contents = GetBootstrapContents(g_fallback_bootstrap_config);
  if (!bootstrap_contents.ok()) return bootstrap_contents.status();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "xDS bootstrap contents: %s",
            bootstrap_contents->c_str());
  }
  // Parse bootstrap.
  grpc_error_handle error = GRPC_ERROR_NONE;
  std::unique_ptr<XdsBootstrap> bootstrap = XdsBootstrap::Create(
      *bootstrap_contents, std::move(certificate_provider_plugin_map), &error);
  if (!GRPC_ERROR_IS_NONE(error)) return grpc_error_to_absl_status(error);
  // Instantiate XdsClient.
  auto xds_client = MakeRefCounted<GrpcXdsClient>(
      std::move(bootstrap), ChannelArgs::FromC(g_channel_args));
  g_xds_client = xds_client.get();
  return xds_client;
}

GrpcXdsClient::GrpcXdsClient(std::unique_ptr<XdsBootstrap> bootstrap,
                             const ChannelArgs& args)
    : XdsClient(
          std::move(bootstrap), MakeOrphanable<GrpcXdsTransportFactory>(args),
          std::max(Duration::Zero(),
                   args.GetDurationFromIntMillis(
                           GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS)
                       .value_or(Duration::Seconds(15)))),
      certificate_provider_store_(MakeOrphanable<CertificateProviderStore>(
          static_cast<const GrpcXdsCertificateProviderPluginMap*>(
              this->bootstrap().certificate_provider_plugin_map())
              ->plugin_map())) {}

GrpcXdsClient::~GrpcXdsClient() {
  MutexLock lock(g_mu);
  if (g_xds_client == this) g_xds_client = nullptr;
}

grpc_pollset_set* GrpcXdsClient::interested_parties() const {
  return reinterpret_cast<GrpcXdsTransportFactory*>(transport_factory())
      ->interested_parties();
}

namespace internal {

void SetXdsChannelArgsForTest(grpc_channel_args* args) {
  MutexLock lock(g_mu);
  g_channel_args = args;
}

void UnsetGlobalXdsClientForTest() {
  MutexLock lock(g_mu);
  g_xds_client = nullptr;
}

void SetXdsFallbackBootstrapConfig(const char* config) {
  MutexLock lock(g_mu);
  gpr_free(g_fallback_bootstrap_config);
  g_fallback_bootstrap_config = gpr_strdup(config);
}

}  // namespace internal

//
// GrpcXdsCertificateProviderPluginMap
//

absl::Status GrpcXdsCertificateProviderPluginMap::AddPlugin(
    const std::string& instance_name, const std::string& plugin_name,
    const Json& config) {
  CertificateProviderFactory* factory =
      CertificateProviderRegistry::LookupCertificateProviderFactory(
          plugin_name);
  if (factory == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unrecognized plugin name: ", plugin_name));
  }
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto parsed_config = factory->CreateCertificateProviderConfig(config, &error);
  if (!GRPC_ERROR_IS_NONE(error)) {
    absl::Status status = grpc_error_to_absl_status(error);
    GRPC_ERROR_UNREF(error);
    return status;
  }
  plugin_map_.insert({instance_name, {plugin_name, std::move(parsed_config)}});
  return absl::OkStatus();
}

bool GrpcXdsCertificateProviderPluginMap::HasPlugin(
    const std::string& instance_name) const {
  return plugin_map_.find(instance_name) != plugin_map_.end();
}

std::string GrpcXdsCertificateProviderPluginMap::ToString() const {
  std::vector<std::string> parts = {"{\n"};
  for (const auto& entry : plugin_map_) {
    parts.push_back(
        absl::StrFormat("  %s={\n"
                        "    plugin_name=%s\n"
                        "    config=%s\n"
                        "  },\n",
                        entry.first, entry.second.plugin_name,
                        entry.second.config->ToString()));
  }
  parts.push_back("}");
  return absl::StrJoin(parts, "");
}

}  // namespace grpc_core

// The returned bytes may contain NULL(0), so we can't use c-string.
grpc_slice grpc_dump_xds_configs(void) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto xds_client = grpc_core::GrpcXdsClient::GetOrCreate(
      grpc_core::ChannelArgs(), "grpc_dump_xds_configs()");
  if (!xds_client.ok()) {
    // If we aren't using xDS, just return an empty string.
    return grpc_empty_slice();
  }
  return grpc_slice_from_cpp_string((*xds_client)->DumpClientConfigBinary());
}
