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

#include <limits.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_cluster_specifier_plugin.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/ext/xds/xds_transport.h"
#include "src/core/ext/xds/xds_transport_grpc.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_refcount.h"

namespace grpc_core {

namespace {

Mutex* g_mu = nullptr;
const grpc_channel_args* g_channel_args ABSL_GUARDED_BY(*g_mu) = nullptr;
XdsClient* g_xds_client ABSL_GUARDED_BY(*g_mu) = nullptr;
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

std::string GetBootstrapContents(const char* fallback_config,
                                 grpc_error_handle* error) {
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
    *error =
        grpc_load_file(path.get(), /*add_null_terminator=*/true, &contents);
    if (!GRPC_ERROR_IS_NONE(*error)) return "";
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
  *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Environment variables GRPC_XDS_BOOTSTRAP or GRPC_XDS_BOOTSTRAP_CONFIG "
      "not defined");
  return "";
}

}  // namespace

RefCountedPtr<XdsClient> GrpcXdsClient::GetOrCreate(
    const grpc_channel_args* args, grpc_error_handle* error) {
  RefCountedPtr<XdsClient> xds_client;
  // If getting bootstrap from channel args, create a local XdsClient
  // instance for the channel or server instead of using the global instance.
  const char* bootstrap_config = grpc_channel_args_find_string(
      args, GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG);
  if (bootstrap_config != nullptr) {
    std::unique_ptr<XdsBootstrap> bootstrap =
        XdsBootstrap::Create(bootstrap_config, error);
    if (GRPC_ERROR_IS_NONE(*error)) {
      grpc_channel_args* xds_channel_args =
          grpc_channel_args_find_pointer<grpc_channel_args>(
              args,
              GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_CLIENT_CHANNEL_ARGS);
      return MakeRefCounted<GrpcXdsClient>(std::move(bootstrap),
                                           xds_channel_args);
    }
    return nullptr;
  }
  // Otherwise, use the global instance.
  {
    MutexLock lock(g_mu);
    if (g_xds_client != nullptr) {
      auto xds_client = g_xds_client->RefIfNonZero();
      if (xds_client != nullptr) return xds_client;
    }
    // Find bootstrap contents.
    std::string bootstrap_contents =
        GetBootstrapContents(g_fallback_bootstrap_config, error);
    if (!GRPC_ERROR_IS_NONE(*error)) return nullptr;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "xDS bootstrap contents: %s",
              bootstrap_contents.c_str());
    }
    // Parse bootstrap.
    std::unique_ptr<XdsBootstrap> bootstrap =
        XdsBootstrap::Create(bootstrap_contents, error);
    if (!GRPC_ERROR_IS_NONE(*error)) return nullptr;
    // Instantiate XdsClient.
    xds_client =
        MakeRefCounted<GrpcXdsClient>(std::move(bootstrap), g_channel_args);
    g_xds_client = xds_client.get();
  }
  return xds_client;
}

namespace {

Duration GetResourceDurationFromArgs(const grpc_channel_args* args) {
  return Duration::Milliseconds(grpc_channel_args_find_integer(
      args, GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS,
      {15000, 0, INT_MAX}));
}

}  // namespace

GrpcXdsClient::GrpcXdsClient(std::unique_ptr<XdsBootstrap> bootstrap,
                             const grpc_channel_args* args)
    : XdsClient(std::move(bootstrap),
                MakeOrphanable<GrpcXdsTransportFactory>(args),
                GetResourceDurationFromArgs(args)) {}

GrpcXdsClient::~GrpcXdsClient() {
  MutexLock lock(g_mu);
  if (g_xds_client == this) g_xds_client = nullptr;
}

grpc_pollset_set* GrpcXdsClient::interested_parties() const {
  return reinterpret_cast<GrpcXdsTransportFactory*>(transport_factory())
      ->interested_parties();
}

#define GRPC_ARG_XDS_CLIENT "grpc.internal.xds_client"

namespace {

void* XdsClientArgCopy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Ref(DEBUG_LOCATION, "channel arg").release();
  return p;
}

void XdsClientArgDestroy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Unref(DEBUG_LOCATION, "channel arg");
}

int XdsClientArgCmp(void* p, void* q) { return QsortCompare(p, q); }

const grpc_arg_pointer_vtable kXdsClientArgVtable = {
    XdsClientArgCopy, XdsClientArgDestroy, XdsClientArgCmp};

}  // namespace

grpc_arg GrpcXdsClient::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(const_cast<char*>(GRPC_ARG_XDS_CLIENT),
                                         const_cast<GrpcXdsClient*>(this),
                                         &kXdsClientArgVtable);
}

RefCountedPtr<XdsClient> GrpcXdsClient::GetFromChannelArgs(
    const grpc_channel_args& args) {
  XdsClient* xds_client =
      grpc_channel_args_find_pointer<XdsClient>(&args, GRPC_ARG_XDS_CLIENT);
  if (xds_client == nullptr) return nullptr;
  return xds_client->Ref(DEBUG_LOCATION, "GetFromChannelArgs");
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
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto xds_client = grpc_core::GrpcXdsClient::GetOrCreate(nullptr, &error);
  if (!GRPC_ERROR_IS_NONE(error)) {
    // If we aren't using xDS, just return an empty string.
    GRPC_ERROR_UNREF(error);
    return grpc_empty_slice();
  }
  return grpc_slice_from_cpp_string(xds_client->DumpClientConfigBinary());
}
