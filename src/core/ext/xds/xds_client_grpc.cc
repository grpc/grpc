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
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_transport.h"
#include "src/core/ext/xds/xds_transport_grpc.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

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

//
// GrpcXdsClient
//

namespace {

Mutex* g_mu = new Mutex;
const grpc_channel_args* g_channel_args ABSL_GUARDED_BY(*g_mu) = nullptr;
GrpcXdsClient* g_xds_client ABSL_GUARDED_BY(*g_mu) = nullptr;
char* g_fallback_bootstrap_config ABSL_GUARDED_BY(*g_mu) = nullptr;

}  // namespace

namespace {

absl::StatusOr<std::string> GetBootstrapContents(const char* fallback_config) {
  // First, try GRPC_XDS_BOOTSTRAP env var.
  auto path = GetEnv("GRPC_XDS_BOOTSTRAP");
  if (path.has_value()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "Got bootstrap file location from GRPC_XDS_BOOTSTRAP "
              "environment variable: %s",
              path->c_str());
    }
    grpc_slice contents;
    grpc_error_handle error =
        grpc_load_file(path->c_str(), /*add_null_terminator=*/true, &contents);
    if (!error.ok()) return grpc_error_to_absl_status(error);
    std::string contents_str(StringViewFromSlice(contents));
    CSliceUnref(contents);
    return contents_str;
  }
  // Next, try GRPC_XDS_BOOTSTRAP_CONFIG env var.
  auto env_config = GetEnv("GRPC_XDS_BOOTSTRAP_CONFIG");
  if (env_config.has_value()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "Got bootstrap contents from GRPC_XDS_BOOTSTRAP_CONFIG "
              "environment variable");
    }
    return std::move(*env_config);
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
        std::move(*bootstrap), channel_args,
        MakeOrphanable<GrpcXdsTransportFactory>(channel_args));
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
  auto bootstrap = GrpcXdsBootstrap::Create(*bootstrap_contents);
  if (!bootstrap.ok()) return bootstrap.status();
  // Instantiate XdsClient.
  auto channel_args = ChannelArgs::FromC(g_channel_args);
  auto xds_client = MakeRefCounted<GrpcXdsClient>(
      std::move(*bootstrap), channel_args,
      MakeOrphanable<GrpcXdsTransportFactory>(channel_args));
  g_xds_client = xds_client.get();
  return xds_client;
}

GrpcXdsClient::GrpcXdsClient(
    std::unique_ptr<GrpcXdsBootstrap> bootstrap, const ChannelArgs& args,
    OrphanablePtr<XdsTransportFactory> transport_factory)
    : XdsClient(
          std::move(bootstrap), std::move(transport_factory),
          grpc_event_engine::experimental::GetDefaultEventEngine(),
          absl::StrCat("gRPC C-core ", GPR_PLATFORM_STRING,
                       GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING),
          absl::StrCat("C-core ", grpc_version_string(),
                       GRPC_XDS_USER_AGENT_NAME_SUFFIX_STRING,
                       GRPC_XDS_USER_AGENT_VERSION_SUFFIX_STRING),
          std::max(Duration::Zero(),
                   args.GetDurationFromIntMillis(
                           GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS)
                       .value_or(Duration::Seconds(15)))),
      certificate_provider_store_(MakeOrphanable<CertificateProviderStore>(
          static_cast<const GrpcXdsBootstrap&>(this->bootstrap())
              .certificate_providers())) {}

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

}  // namespace grpc_core

// The returned bytes may contain NULL(0), so we can't use c-string.
grpc_slice grpc_dump_xds_configs(void) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  auto xds_client = grpc_core::GrpcXdsClient::GetOrCreate(
      grpc_core::ChannelArgs(), "grpc_dump_xds_configs()");
  if (!xds_client.ok()) {
    // If we aren't using xDS, just return an empty string.
    return grpc_empty_slice();
  }
  return grpc_slice_from_cpp_string((*xds_client)->DumpClientConfigBinary());
}
