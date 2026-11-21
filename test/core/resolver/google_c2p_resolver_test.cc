//
// Copyright 2017 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/alts/check_gcp_environment.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/env.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/core/util/json/json.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/uri.h"
#include "src/core/util/work_serializer.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_client_grpc.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "test/core/test_util/fake_udp_and_tcp_server.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

extern void RegisterCloud2ProdResolver(CoreConfiguration::Builder* builder);

namespace testing {
namespace {

constexpr absl::string_view kPretendRunningOnGcpArg =
    "grpc.testing.google_c2p_resolver_pretend_running_on_gcp";
constexpr absl::string_view kMetadataServerOverrideArg =
    "grpc.testing.google_c2p_resolver_metadata_server_override";
constexpr absl::string_view kC2PAuthority =
    "traffic-director-c2p.xds.googleapis.com";

struct ChildResolverState {
  std::string scheme;
  URI uri;
  ChannelArgs args;
  Notification started;
  int reresolution_count = 0;
  int reset_backoff_count = 0;
  bool shutdown = false;
  RefCountedPtr<GrpcXdsClient> xds_client;
};

class FakeChildResolver final : public Resolver {
 public:
  FakeChildResolver(absl::string_view scheme, ResolverArgs args,
                    std::shared_ptr<ChildResolverState> state)
      : state_(std::move(state)) {
    state_->scheme = std::string(scheme);
    state_->uri = std::move(args.uri);
    state_->args = std::move(args.args);
  }

  void StartLocked() override {
    if (state_->scheme == "xds") {
      auto xds_client = GrpcXdsClient::GetOrCreate(
          state_->uri.ToString(), state_->args, "fake xds resolver");
      if (xds_client.ok()) {
        state_->xds_client = std::move(*xds_client);
      }
    }
    state_->started.Notify();
  }

  void RequestReresolutionLocked() override { ++state_->reresolution_count; }

  void ResetBackoffLocked() override { ++state_->reset_backoff_count; }

  void ShutdownLocked() override {
    state_->shutdown = true;
    state_->xds_client.reset();
  }

 private:
  std::shared_ptr<ChildResolverState> state_;
};

class FakeChildResolverFactory final : public ResolverFactory {
 public:
  FakeChildResolverFactory(absl::string_view scheme,
                           std::shared_ptr<ChildResolverState> state)
      : scheme_(scheme), state_(std::move(state)) {}

  absl::string_view scheme() const override { return scheme_; }

  bool IsValidUri(const URI& /*uri*/) const override { return true; }

  OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const override {
    return MakeOrphanable<FakeChildResolver>(scheme_, std::move(args), state_);
  }

 private:
  std::string scheme_;
  std::shared_ptr<ChildResolverState> state_;
};

class NoOpResultHandler final : public Resolver::ResultHandler {
 public:
  void ReportResult(Resolver::Result /*result*/) override {}
};

// Metadata server HTTP GET override state for unit tests.
struct FakeMetadataServerState {
  std::atomic<int> num_zone_queries{0};
  std::string expected_authority = "metadata.google.internal.";
  int status = 200;
  std::string response_body = "projects/123456/zones/us-central1-a";
};

FakeMetadataServerState* g_metadata_state = nullptr;

int HttpGetOverride(const grpc_http_request* request, const URI& uri,
                    Timestamp /*deadline*/, grpc_closure* on_done,
                    grpc_http_response* response) {
  if (g_metadata_state == nullptr) return 0;
  if (uri.path() != "/computeMetadata/v1/instance/zone") return 0;
  g_metadata_state->num_zone_queries.fetch_add(1);
  EXPECT_EQ(uri.authority(), g_metadata_state->expected_authority);
  EXPECT_EQ(request->hdr_count, 1u);
  if (request->hdr_count >= 1) {
    EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Metadata-Flavor");
    EXPECT_EQ(absl::string_view(request->hdrs[0].value), "Google");
  }
  response->status = g_metadata_state->status;
  if (!g_metadata_state->response_body.empty()) {
    response->body = gpr_strdup(g_metadata_state->response_body.c_str());
    response->body_length = g_metadata_state->response_body.size();
  }
  ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
  return 1;
}

template <typename TestFn>
void RunWithFakeChildResolvers(TestFn test_fn) {
  CoreConfiguration::RunWithSpecialConfiguration(
      [](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->resolver_registry()->Reset();
        RegisterCloud2ProdResolver(builder);
      },
      [&]() { test_fn(); });
}

std::shared_ptr<ChildResolverState> BuildAndStartResolver(
    absl::string_view target, ChannelArgs args = ChannelArgs(),
    std::function<void(Resolver*, const ChildResolverState&)> before_shutdown =
        nullptr) {
  auto state = std::make_shared<ChildResolverState>();
  CoreConfiguration::RunWithSpecialConfiguration(
      [&](CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->resolver_registry()->Reset();
        RegisterCloud2ProdResolver(builder);
        builder->resolver_registry()->RegisterResolverFactory(
            std::make_unique<FakeChildResolverFactory>("dns", state));
        builder->resolver_registry()->RegisterResolverFactory(
            std::make_unique<FakeChildResolverFactory>("xds", state));
      },
      [&]() {
        ExecCtx exec_ctx;
        auto work_serializer = std::make_shared<WorkSerializer>(
            grpc_event_engine::experimental::GetDefaultEventEngine());
        ChannelArgs resolver_args = args.SetObject(ResourceQuota::Default());
        OrphanablePtr<Resolver> resolver =
            CoreConfiguration::Get().resolver_registry().CreateResolver(
                target, resolver_args, /*pollset_set=*/nullptr, work_serializer,
                std::make_unique<NoOpResultHandler>());
        ASSERT_NE(resolver, nullptr);
        Notification started_on_serializer;
        work_serializer->Run([&]() {
          resolver->StartLocked();
          started_on_serializer.Notify();
        });
        ExecCtx::Get()->Flush();
        started_on_serializer.WaitForNotification();
        state->started.WaitForNotification();
        Notification done_on_serializer;
        work_serializer->Run([&]() {
          if (before_shutdown != nullptr) {
            before_shutdown(resolver.get(), *state);
          }
          resolver.reset();
          done_on_serializer.Notify();
        });
        ExecCtx::Get()->Flush();
        done_on_serializer.WaitForNotification();
      });
  return state;
}

void VerifyServerConfig(const XdsBootstrap::XdsServer* server,
                        absl::string_view expected_server_uri) {
  ASSERT_NE(server, nullptr);
  const auto* grpc_server = static_cast<const GrpcXdsServer*>(server);
  ASSERT_NE(grpc_server->target(), nullptr);
  EXPECT_EQ(grpc_server->target()->server_uri(), expected_server_uri);
  EXPECT_TRUE(grpc_server->IgnoreResourceDeletion());
  const auto& grpc_target =
      DownCast<const GrpcXdsServerTarget&>(*grpc_server->target());
  ASSERT_NE(grpc_target.channel_creds_config(), nullptr);
  EXPECT_EQ(grpc_target.channel_creds_config()->type(), "google_default");
}

void VerifyBootstrapConfig(const XdsBootstrap& bootstrap,
                           absl::string_view expected_node_id_prefix,
                           absl::string_view expected_zone,
                           absl::string_view expected_server_uri) {
  const XdsBootstrap::Node* node = bootstrap.node();
  ASSERT_NE(node, nullptr);
  EXPECT_TRUE(absl::StartsWith(node->id(), expected_node_id_prefix))
      << "node id: " << node->id()
      << ", expected prefix: " << expected_node_id_prefix;
  if (expected_node_id_prefix == "C2P-") {
    EXPECT_FALSE(absl::StartsWith(node->id(), "C2P-non-gcp-"))
        << "node id: " << node->id();
  }
  EXPECT_EQ(node->locality_zone(), expected_zone);
  auto it =
      node->metadata().find("TRAFFICDIRECTOR_DIRECTPATH_C2P_IPV6_CAPABLE");
  ASSERT_NE(it, node->metadata().end());
  EXPECT_EQ(it->second.type(), Json::Type::kBoolean);
  EXPECT_TRUE(it->second.boolean());

  ASSERT_EQ(bootstrap.servers().size(), 1u);
  VerifyServerConfig(bootstrap.servers()[0], expected_server_uri);

  const XdsBootstrap::Authority* authority =
      bootstrap.LookupAuthority(std::string(kC2PAuthority));
  ASSERT_NE(authority, nullptr);
  ASSERT_EQ(authority->servers().size(), 1u);
  VerifyServerConfig(authority->servers()[0], expected_server_uri);
}

// Because GoogleCloud2ProdResolver::StartXdsResolver() caches the result of
// ConstructBootstrap() in a function-local static, each test that verifies a
// distinct bootstrap configuration runs in a fresh child process via
// EXPECT_EXIT. Naming the test suite *DeathTest ensures GoogleTest runs these
// before any in-process test initializes the static bootstrap or starts
// threads.
template <typename SubprocessFn>
void RunInFreshProcess(SubprocessFn fn) {
  EXPECT_EXIT(
      {
        grpc_init();
        FakeMetadataServerState metadata_state;
        g_metadata_state = &metadata_state;
        HttpRequest::SetOverride(HttpGetOverride, nullptr, nullptr);
        fn(&metadata_state);
        HttpRequest::SetOverride(nullptr, nullptr, nullptr);
        g_metadata_state = nullptr;
        grpc_shutdown();
        fflush(stdout);
        fflush(stderr);
        _exit(::testing::Test::HasFailure() ? 1 : 0);
      },
      ::testing::ExitedWithCode(0), "");
}

TEST(GoogleCloud2ProdResolverBootstrapDeathTest,
     OnGcpConstructsBootstrapWithZone) {
  RunInFreshProcess([](FakeMetadataServerState* metadata_state) {
    metadata_state->expected_authority = "custom.metadata.server.";
    metadata_state->response_body = "projects/123456/zones/us-west1-b";
    ChannelArgs args =
        ChannelArgs()
            .Set(kPretendRunningOnGcpArg, true)
            .Set(kMetadataServerOverrideArg, "custom.metadata.server.");
    BuildAndStartResolver(
        "google-c2p:///googleapis.com", args,
        [](Resolver* /*resolver*/, const ChildResolverState& state) {
          ASSERT_NE(state.xds_client, nullptr);
          VerifyBootstrapConfig(state.xds_client->bootstrap(), "C2P-",
                                "us-west1-b", "directpath-pa.googleapis.com");
        });
    EXPECT_EQ(metadata_state->num_zone_queries.load(), 1);
  });
}

TEST(GoogleCloud2ProdResolverBootstrapDeathTest,
     OnGcpMetadataQueryFailureOmitsZoneInBootstrap) {
  RunInFreshProcess([](FakeMetadataServerState* metadata_state) {
    metadata_state->status = 500;
    ChannelArgs args = ChannelArgs().Set(kPretendRunningOnGcpArg, true);
    BuildAndStartResolver(
        "google-c2p:///googleapis.com", args,
        [](Resolver* /*resolver*/, const ChildResolverState& state) {
          ASSERT_NE(state.xds_client, nullptr);
          VerifyBootstrapConfig(state.xds_client->bootstrap(), "C2P-",
                                /*expected_zone=*/"",
                                "directpath-pa.googleapis.com");
        });
    EXPECT_EQ(metadata_state->num_zone_queries.load(), 1);
  });
}

TEST(GoogleCloud2ProdResolverBootstrapDeathTest,
     TrafficDirectorUriOverrideInBootstrap) {
  RunInFreshProcess([](FakeMetadataServerState* /*metadata_state*/) {
    ScopedEnvVar td_uri_env(
        "GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI",
        "custom-td.googleapis.com");
    ChannelArgs args = ChannelArgs().Set(kPretendRunningOnGcpArg, true);
    BuildAndStartResolver(
        "google-c2p:///googleapis.com", args,
        [](Resolver* /*resolver*/, const ChildResolverState& state) {
          ASSERT_NE(state.xds_client, nullptr);
          VerifyBootstrapConfig(state.xds_client->bootstrap(), "C2P-",
                                "us-central1-a", "custom-td.googleapis.com");
        });
  });
}

TEST(GoogleCloud2ProdResolverBootstrapDeathTest,
     ForceXdsConstructsBootstrapWithNonGcpNodeIdPrefix) {
  RunInFreshProcess([](FakeMetadataServerState* metadata_state) {
    BuildAndStartResolver(
        "google-c2p:///googleapis.com?force-xds", ChannelArgs(),
        [](Resolver* /*resolver*/, const ChildResolverState& state) {
          ASSERT_NE(state.xds_client, nullptr);
          VerifyBootstrapConfig(state.xds_client->bootstrap(), "C2P-non-gcp-",
                                /*expected_zone=*/"",
                                "directpath-pa.googleapis.com");
        });
    EXPECT_EQ(metadata_state->num_zone_queries.load(), 0);
  });
}

class GoogleCloud2ProdResolverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    g_metadata_state = &metadata_state_;
    HttpRequest::SetOverride(HttpGetOverride, nullptr, nullptr);
  }

  void TearDown() override {
    HttpRequest::SetOverride(nullptr, nullptr, nullptr);
    g_metadata_state = nullptr;
    grpc_shutdown();
  }

  FakeMetadataServerState metadata_state_;
};

TEST_F(GoogleCloud2ProdResolverTest, InvalidUriWithAuthorityRejected) {
  RunWithFakeChildResolvers([]() {
    ExecCtx exec_ctx;
    auto work_serializer = std::make_shared<WorkSerializer>(
        grpc_event_engine::experimental::GetDefaultEventEngine());
    ChannelArgs args = ChannelArgs().SetObject(ResourceQuota::Default());
    for (absl::string_view target :
         {"google-c2p://an-authority/resource",
          "google-c2p-experimental://an-authority/resource"}) {
      EXPECT_FALSE(
          CoreConfiguration::Get().resolver_registry().IsValidTarget(target))
          << target;
      OrphanablePtr<Resolver> resolver =
          CoreConfiguration::Get().resolver_registry().CreateResolver(
              target, args, /*pollset_set=*/nullptr, work_serializer,
              std::make_unique<NoOpResultHandler>());
      EXPECT_EQ(resolver, nullptr) << target;
    }
  });
}

TEST_F(GoogleCloud2ProdResolverTest, NotOnGcpDelegatesToDns) {
  if (grpc_alts_is_running_on_gcp()) {
    GTEST_SKIP() << "Host is running on GCP; cannot test non-GCP fallback "
                    "without overriding grpc_alts_is_running_on_gcp()";
  }
  auto state = BuildAndStartResolver("google-c2p:///googleapis.com");
  EXPECT_EQ(state->scheme, "dns");
  EXPECT_EQ(state->uri.path(), "googleapis.com");
  EXPECT_EQ(metadata_state_.num_zone_queries.load(), 0);
}

TEST_F(GoogleCloud2ProdResolverTest, UppercaseForceXdsQueryParamIsIgnored) {
  ScopedEnvVar federation_env("GRPC_EXPERIMENTAL_XDS_FEDERATION", "false");
  ScopedEnvVar bootstrap_env("GRPC_XDS_BOOTSTRAP", "does_not_matter");
  auto state = BuildAndStartResolver("google-c2p:///googleapis.com?FORCE-XDS");
  EXPECT_EQ(state->scheme, "dns");
  EXPECT_EQ(state->uri.path(), "googleapis.com");
  EXPECT_EQ(metadata_state_.num_zone_queries.load(), 0);
}

TEST_F(GoogleCloud2ProdResolverTest,
       OnGcpWithBootstrapEnvAndFederationDisabledDelegatesToDns) {
  ScopedEnvVar federation_env("GRPC_EXPERIMENTAL_XDS_FEDERATION", "false");
  for (const char* bootstrap_env_name :
       {"GRPC_XDS_BOOTSTRAP", "GRPC_XDS_BOOTSTRAP_CONFIG"}) {
    ScopedEnvVar bootstrap_env(bootstrap_env_name, "does_not_matter");
    ChannelArgs args = ChannelArgs().Set(kPretendRunningOnGcpArg, true);
    auto state = BuildAndStartResolver("google-c2p:///googleapis.com", args);
    EXPECT_EQ(state->scheme, "dns") << bootstrap_env_name;
    EXPECT_EQ(state->uri.path(), "googleapis.com") << bootstrap_env_name;
    EXPECT_EQ(metadata_state_.num_zone_queries.load(), 0) << bootstrap_env_name;
  }
}

TEST_F(GoogleCloud2ProdResolverTest,
       OnGcpWithBootstrapEnvAndFederationEnabledDelegatesToXds) {
  for (const char* bootstrap_env_name :
       {"GRPC_XDS_BOOTSTRAP", "GRPC_XDS_BOOTSTRAP_CONFIG"}) {
    ScopedEnvVar bootstrap_env(bootstrap_env_name, "does_not_matter");
    metadata_state_.num_zone_queries.store(0);
    ChannelArgs args = ChannelArgs().Set(kPretendRunningOnGcpArg, true);
    auto state = BuildAndStartResolver("google-c2p:///googleapis.com", args);
    EXPECT_EQ(state->scheme, "xds") << bootstrap_env_name;
    EXPECT_EQ(state->uri.authority(), kC2PAuthority) << bootstrap_env_name;
    EXPECT_EQ(state->uri.path(), "/googleapis.com") << bootstrap_env_name;
    EXPECT_EQ(metadata_state_.num_zone_queries.load(), 1) << bootstrap_env_name;
  }
}

TEST_F(
    GoogleCloud2ProdResolverTest,
    OnGcpWithFederationDisabledAndNoBootstrapEnvDelegatesToXdsWithoutAuthority) {
  ScopedEnvVar federation_env("GRPC_EXPERIMENTAL_XDS_FEDERATION", "false");
  ChannelArgs args = ChannelArgs().Set(kPretendRunningOnGcpArg, true);
  auto state = BuildAndStartResolver("google-c2p:///googleapis.com", args);
  EXPECT_EQ(state->scheme, "xds");
  EXPECT_EQ(state->uri.authority(), "");
  EXPECT_EQ(state->uri.path(), "googleapis.com");
  EXPECT_EQ(metadata_state_.num_zone_queries.load(), 1);
}

TEST_F(GoogleCloud2ProdResolverTest, ExperimentalSchemeDelegatesToXds) {
  ChannelArgs args = ChannelArgs().Set(kPretendRunningOnGcpArg, true);
  auto state =
      BuildAndStartResolver("google-c2p-experimental:///googleapis.com", args);
  EXPECT_EQ(state->scheme, "xds");
  EXPECT_EQ(state->uri.authority(), kC2PAuthority);
  EXPECT_EQ(state->uri.path(), "/googleapis.com");
  EXPECT_EQ(metadata_state_.num_zone_queries.load(), 1);
}

TEST_F(GoogleCloud2ProdResolverTest,
       ForwardsReresolutionAndResetBackoffAndShutdownToChild) {
  ChannelArgs args = ChannelArgs().Set(kPretendRunningOnGcpArg, true);
  auto state = BuildAndStartResolver(
      "google-c2p:///googleapis.com", args,
      [](Resolver* resolver, const ChildResolverState& /*state*/) {
        resolver->RequestReresolutionLocked();
        resolver->ResetBackoffLocked();
      });
  EXPECT_EQ(state->reresolution_count, 1);
  EXPECT_EQ(state->reset_backoff_count, 1);
  EXPECT_TRUE(state->shutdown);
}

TEST_F(GoogleCloud2ProdResolverTest,
       ForceXdsQueryParamVariantsDelegateToXdsWithoutMetadataQuery) {
  struct TestCase {
    absl::string_view target;
    bool pretend_on_gcp;
  };
  const TestCase kTestCases[] = {
      {"google-c2p:///googleapis.com?force-xds", false},
      {"google-c2p:///googleapis.com?force-xds=", false},
      {"google-c2p:///googleapis.com?force-xds=true", false},
      {"google-c2p:///googleapis.com?force-xds=false", false},
      {"google-c2p:///googleapis.com?foo=bar&force-xds", false},
      {"google-c2p:///googleapis.com?force-xds", true},
  };
  for (const auto& tc : kTestCases) {
    metadata_state_.num_zone_queries.store(0);
    ChannelArgs args;
    if (tc.pretend_on_gcp) {
      args = args.Set(kPretendRunningOnGcpArg, true);
    }
    auto state = BuildAndStartResolver(tc.target, args);
    EXPECT_EQ(state->scheme, "xds") << tc.target;
    EXPECT_EQ(state->uri.authority(), kC2PAuthority) << tc.target;
    EXPECT_EQ(state->uri.path(), "/googleapis.com") << tc.target;
    EXPECT_TRUE(state->uri.query_parameter_map().empty()) << tc.target;
    EXPECT_EQ(metadata_state_.num_zone_queries.load(), 0) << tc.target;
  }
}

void TryConnectAndDestroy(const char* fake_metadata_server_address) {
  grpc::ChannelArguments args;
  std::string target = "google-c2p:///servername_not_used";
  args.SetInt("grpc.testing.google_c2p_resolver_pretend_running_on_gcp", 1);
  args.SetString("grpc.testing.google_c2p_resolver_metadata_server_override",
                 fake_metadata_server_address);
  auto channel = grpc::CreateCustomChannel(
      target, grpc::InsecureChannelCredentials(), args);
  // Start connecting, and give some time for the google-c2p resolver to begin
  // resolution and start trying to contact the metadata server.
  channel->GetState(true /* try_to_connect */);
  ASSERT_FALSE(
      channel->WaitForConnected(grpc_timeout_milliseconds_to_deadline(100)));
  channel.reset();
}

class DestroyGoogleC2pChannelWithActiveConnectStressTest
    : public ::testing::Test {
 protected:
  void SetUp() override {
    grpc_init();
    fake_xds_server_ = std::make_unique<FakeUdpAndTcpServer>(
        FakeUdpAndTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
        FakeUdpAndTcpServer::CloseSocketUponReceivingBytesFromPeer);
    SetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI",
           fake_xds_server_->address());
  }

  void TearDown() override {
    UnsetEnv("GRPC_TEST_ONLY_GOOGLE_C2P_RESOLVER_TRAFFIC_DIRECTOR_URI");
    fake_xds_server_.reset();
    grpc_shutdown();
  }

  std::unique_ptr<FakeUdpAndTcpServer> fake_xds_server_;
};

// Exercise the machinery involved with shutting down the C2P resolver while
// it's waiting for its initial metadata server queries to finish.
TEST_F(DestroyGoogleC2pChannelWithActiveConnectStressTest,
       LoopTryConnectAndDestroyWithHangingMetadataServer) {
  // Create a fake metadata server which hangs.
  FakeUdpAndTcpServer fake_metadata_server(
      FakeUdpAndTcpServer::AcceptMode::kWaitForClientToSendFirstBytes,
      FakeUdpAndTcpServer::CloseSocketUponCloseFromPeer);
  std::vector<std::unique_ptr<std::thread>> threads;
  const int kNumThreads = 10;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        new std::thread(TryConnectAndDestroy, fake_metadata_server.address()));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
}

// Exercise the machinery involved with shutting down the C2P resolver while
// it's waiting for its initial metadata server queries to finish.
TEST_F(DestroyGoogleC2pChannelWithActiveConnectStressTest,
       LoopTryConnectAndDestroyWithFastFailingMetadataServer) {
  // Create a fake metadata server address which rejects connections
  int port = grpc_pick_unused_port_or_die();
  std::string address = absl::StrFormat("[::1]:%d", port);
  std::vector<std::unique_ptr<std::thread>> threads;
  const int kNumThreads = 10;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.emplace_back(
        new std::thread(TryConnectAndDestroy, address.c_str()));
  }
  for (size_t i = 0; i < threads.size(); i++) {
    threads[i]->join();
  }
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
