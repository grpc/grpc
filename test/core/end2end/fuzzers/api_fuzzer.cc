//
//
// Copyright 2016 gRPC authors.
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

#include <google/protobuf/text_format.h>
#include <grpc/credentials.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "src/core/ext/transport/inproc/inproc_transport.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/env.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"
#include "test/core/end2end/fuzzers/fuzzing_common.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/test_util/fuzz_config_vars.h"
#include "test/core/test_util/fuzz_config_vars_helpers.h"
#include "test/core/test_util/fuzzing_channel_args.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

// IWYU pragma: no_include <google/protobuf/repeated_ptr_field.h>

////////////////////////////////////////////////////////////////////////////////
// dns resolution

typedef struct addr_req {
  char* addr;
  grpc_closure* on_done;
  std::unique_ptr<grpc_core::EndpointAddressesList>* addresses;
} addr_req;

static void finish_resolve(addr_req r) {
  if (0 == strcmp(r.addr, "server")) {
    *r.addresses = std::make_unique<grpc_core::EndpointAddressesList>();
    (*r.addresses)->emplace_back("ipv4:1.2.3.4:5", grpc_core::ChannelArgs());
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, r.on_done, absl::OkStatus());
  } else {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, r.on_done,
                            absl::UnknownError("Resolution failed"));
  }
  gpr_free(r.addr);
}

namespace {

using grpc_event_engine::experimental::FuzzingEventEngine;
using grpc_event_engine::experimental::GetDefaultEventEngine;

class FuzzerDNSResolver : public grpc_core::DNSResolver {
 public:
  class FuzzerDNSRequest {
   public:
    FuzzerDNSRequest(
        absl::string_view name,
        std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
            on_done)
        : name_(std::string(name)), on_done_(std::move(on_done)) {
      GetDefaultEventEngine()->RunAfter(grpc_core::Duration::Seconds(1),
                                        [this] {
                                          grpc_core::ExecCtx exec_ctx;
                                          FinishResolve();
                                        });
    }

   private:
    void FinishResolve() {
      if (name_ == "server") {
        std::vector<grpc_resolved_address> addrs;
        grpc_resolved_address addr;
        memset(&addr, 0, sizeof(addr));
        addrs.push_back(addr);
        on_done_(std::move(addrs));
      } else {
        on_done_(absl::UnknownError("Resolution failed"));
      }
      delete this;
    }

    const std::string name_;
    const std::function<void(
        absl::StatusOr<std::vector<grpc_resolved_address>>)>
        on_done_;
  };

  explicit FuzzerDNSResolver(FuzzingEventEngine* engine) : engine_(engine) {}

  TaskHandle LookupHostname(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view name, absl::string_view /* default_port */,
      grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    new FuzzerDNSRequest(name, std::move(on_resolved));
    return kNullHandle;
  }

  absl::StatusOr<std::vector<grpc_resolved_address>> LookupHostnameBlocking(
      absl::string_view name, absl::string_view default_port) override {
    // To mimic the resolution delay
    absl::SleepFor(absl::Seconds(1));
    if (name == "server") {
      std::vector<grpc_resolved_address> addrs;
      grpc_resolved_address addr;
      memset(&addr, 0, sizeof(addr));
      addrs.push_back(addr);
      return addrs;
    } else {
      return absl::UnknownError("Resolution failed");
    }
  }

  TaskHandle LookupSRV(
      std::function<void(absl::StatusOr<std::vector<grpc_resolved_address>>)>
          on_resolved,
      absl::string_view /* name */, grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    engine_->Run([on_resolved] {
      grpc_core::ExecCtx exec_ctx;
      on_resolved(absl::UnimplementedError(
          "The Fuzzing DNS resolver does not support looking up SRV records"));
    });
    return {-1, -1};
  };

  TaskHandle LookupTXT(
      std::function<void(absl::StatusOr<std::string>)> on_resolved,
      absl::string_view /* name */, grpc_core::Duration /* timeout */,
      grpc_pollset_set* /* interested_parties */,
      absl::string_view /* name_server */) override {
    // Not supported
    engine_->Run([on_resolved] {
      grpc_core::ExecCtx exec_ctx;
      on_resolved(absl::UnimplementedError(
          "The Fuzing DNS resolver does not support looking up TXT records"));
    });
    return {-1, -1};
  };

  // FuzzerDNSResolver does not support cancellation.
  bool Cancel(TaskHandle /*handle*/) override { return false; }

 private:
  FuzzingEventEngine* engine_;
};

}  // namespace

#if GRPC_ARES == 1
grpc_ares_request* my_dns_lookup_hostname_ares(
    const char* /*dns_server*/, const char* addr, const char* /*default_port*/,
    grpc_pollset_set* /*interested_parties*/, grpc_closure* on_done,
    std::unique_ptr<grpc_core::EndpointAddressesList>* addresses,
    int /*query_timeout*/) {
  addr_req r;
  r.addr = gpr_strdup(addr);
  r.on_done = on_done;
  r.addresses = addresses;
  GetDefaultEventEngine()->RunAfter(grpc_core::Duration::Seconds(1), [r] {
    grpc_core::ExecCtx exec_ctx;
    finish_resolve(r);
  });
  return nullptr;
}

grpc_ares_request* my_dns_lookup_srv_ares(
    const char* /*dns_server*/, const char* name,
    grpc_pollset_set* /*interested_parties*/, grpc_closure* on_done,
    std::unique_ptr<grpc_core::EndpointAddressesList>* balancer_addresses,
    int /*query_timeout*/) {
  addr_req r;
  r.addr = gpr_strdup(name);
  r.on_done = on_done;
  r.addresses = balancer_addresses;
  GetDefaultEventEngine()->RunAfter(grpc_core::Duration::Seconds(1), [r] {
    grpc_core::ExecCtx exec_ctx;
    finish_resolve(r);
  });
  return nullptr;
}

static void my_cancel_ares_request(grpc_ares_request* request) {
  GRPC_CHECK_NE(request, nullptr);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// globals

namespace grpc_core {
namespace testing {

class ApiFuzzer final : public BasicFuzzer {
 public:
  explicit ApiFuzzer(const fuzzing_event_engine::Actions& actions);
  ~ApiFuzzer();
  void Tick() override;
  grpc_server* Server() { return server_; }

 private:
  Result CreateChannel(
      const api_fuzzer::CreateChannel& create_channel) override;

  Result CreateServer(const api_fuzzer::CreateServer& create_server) override;
  void DestroyServer() override;
  void DestroyChannel() override;

  grpc_server* server() override { return server_; }
  grpc_channel* channel() override { return channel_; }

  grpc_server* server_ = nullptr;
  grpc_channel* channel_ = nullptr;
  std::atomic<bool> channel_force_delete_{false};
};

}  // namespace testing
}  // namespace grpc_core

////////////////////////////////////////////////////////////////////////////////
// test driver

static const char* ReadCredArtifact(
    const api_fuzzer::CredArtifact& artifact,
    std::initializer_list<const char*> builtins) {
  switch (artifact.type_case()) {
    case api_fuzzer::CredArtifact::kCustom:
      return artifact.custom().c_str();
    case api_fuzzer::CredArtifact::kBuiltin:
      if (artifact.builtin() < 0) return nullptr;
      if (artifact.builtin() < static_cast<int>(builtins.size())) {
        return *(builtins.begin() + artifact.builtin());
      }
      return nullptr;
    case api_fuzzer::CredArtifact::TYPE_NOT_SET:
      return nullptr;
  }
}

static grpc_channel_credentials* ReadSslChannelCreds(
    const api_fuzzer::SslChannelCreds& creds) {
  const char* root_certs =
      creds.has_root_certs()
          ? ReadCredArtifact(creds.root_certs(), {test_root_cert})
          : nullptr;
  const char* private_key =
      creds.has_private_key()
          ? ReadCredArtifact(creds.private_key(),
                             {test_server1_key, test_self_signed_client_key,
                              test_signed_client_key})
          : nullptr;
  const char* certs =
      creds.has_certs()
          ? ReadCredArtifact(creds.certs(),
                             {test_server1_cert, test_self_signed_client_cert,
                              test_signed_client_cert})
          : nullptr;
  grpc_ssl_pem_key_cert_pair key_cert_pair = {private_key, certs};
  return grpc_ssl_credentials_create(
      root_certs,
      private_key != nullptr && certs != nullptr ? &key_cert_pair : nullptr,
      nullptr, nullptr);
}

static grpc_call_credentials* ReadCallCreds(
    const api_fuzzer::CallCreds& creds) {
  switch (creds.type_case()) {
    case api_fuzzer::CallCreds::TYPE_NOT_SET:
      return nullptr;
    case api_fuzzer::CallCreds::kNull:
      return nullptr;
    case api_fuzzer::CallCreds::kCompositeCallCreds: {
      grpc_call_credentials* out = nullptr;
      for (const auto& child_creds :
           creds.composite_call_creds().call_creds()) {
        grpc_call_credentials* child = ReadCallCreds(child_creds);
        if (child != nullptr) {
          if (out == nullptr) {
            out = child;
          } else {
            auto* composed =
                grpc_composite_call_credentials_create(out, child, nullptr);
            grpc_call_credentials_release(child);
            grpc_call_credentials_release(out);
            out = composed;
          }
        }
      }
      return out;
    }
    case api_fuzzer::CallCreds::kAccessToken:
      return grpc_access_token_credentials_create(creds.access_token().c_str(),
                                                  nullptr);
    case api_fuzzer::CallCreds::kIam:
      return grpc_google_iam_credentials_create(
          creds.iam().auth_token().c_str(), creds.iam().auth_selector().c_str(),
          nullptr);
      // TODO(ctiller): more cred types here
  }
}

static grpc_channel_credentials* ReadChannelCreds(
    const api_fuzzer::ChannelCreds& creds) {
  switch (creds.type_case()) {
    case api_fuzzer::ChannelCreds::TYPE_NOT_SET:
      return nullptr;
    case api_fuzzer::ChannelCreds::kSslChannelCreds:
      return ReadSslChannelCreds(creds.ssl_channel_creds());
    case api_fuzzer::ChannelCreds::kCompositeChannelCreds: {
      const auto& comp = creds.composite_channel_creds();
      grpc_channel_credentials* c1 =
          comp.has_channel_creds() ? ReadChannelCreds(comp.channel_creds())
                                   : nullptr;
      grpc_call_credentials* c2 =
          comp.has_call_creds() ? ReadCallCreds(comp.call_creds()) : nullptr;
      if (c1 != nullptr && c2 != nullptr) {
        grpc_channel_credentials* out =
            grpc_composite_channel_credentials_create(c1, c2, nullptr);
        grpc_channel_credentials_release(c1);
        grpc_call_credentials_release(c2);
        return out;
      } else if (c1 != nullptr) {
        return c1;
      } else if (c2 != nullptr) {
        grpc_call_credentials_release(c2);
        return nullptr;
      } else {
        return nullptr;
      }
      GPR_UNREACHABLE_CODE(return nullptr);
    }
    case api_fuzzer::ChannelCreds::kNull:
      return nullptr;
  }
}

static grpc_server_credentials* ReadServerCreds(
    const api_fuzzer::ServerCreds& creds) {
  switch (creds.type_case()) {
    case api_fuzzer::ServerCreds::TYPE_NOT_SET:
      return nullptr;
    case api_fuzzer::ServerCreds::kInsecureCreds:
      return grpc_insecure_server_credentials_create();
    case api_fuzzer::ServerCreds::kNull:
      return nullptr;
  }
}

namespace grpc_core {
namespace testing {

ApiFuzzer::ApiFuzzer(const fuzzing_event_engine::Actions& actions)
    : BasicFuzzer(actions) {
  ResetDNSResolver(std::make_unique<FuzzerDNSResolver>(engine().get()));
#if GRPC_ARES == 1
  grpc_dns_lookup_hostname_ares = my_dns_lookup_hostname_ares;
  grpc_dns_lookup_srv_ares = my_dns_lookup_srv_ares;
  grpc_cancel_ares_request = my_cancel_ares_request;
#endif

  GRPC_CHECK_EQ(channel_, nullptr);
  GRPC_CHECK_EQ(server_, nullptr);
}

ApiFuzzer::~ApiFuzzer() {
  GRPC_CHECK_EQ(channel_, nullptr);
  GRPC_CHECK_EQ(server_, nullptr);
}

void ApiFuzzer::Tick() {
  BasicFuzzer::Tick();
  if (channel_force_delete_.exchange(false) && channel_) {
    grpc_channel_destroy(channel_);
    channel_ = nullptr;
  }
}

namespace {

// If there are more than 1K comma-delimited strings in target, remove
// the extra ones.
std::string SanitizeTargetUri(absl::string_view target) {
  constexpr size_t kMaxCommaDelimitedStrings = 1000;
  std::vector<absl::string_view> parts = absl::StrSplit(target, ',');
  if (parts.size() > kMaxCommaDelimitedStrings) {
    parts.resize(kMaxCommaDelimitedStrings);
  }
  return absl::StrJoin(parts, ",");
}

}  // namespace

ApiFuzzer::Result ApiFuzzer::CreateChannel(
    const api_fuzzer::CreateChannel& create_channel) {
  if (channel_ != nullptr) return Result::kComplete;
  // ExecCtx is needed for ChannelArgs destruction.
  ExecCtx exec_ctx;
  testing::FuzzingEnvironment fuzzing_env;
  fuzzing_env.resource_quota = resource_quota();
  ChannelArgs args = testing::CreateChannelArgsFromFuzzingConfiguration(
      create_channel.channel_args(), fuzzing_env);
  if (create_channel.inproc()) {
    if (server_ == nullptr) return Result::kFailed;
    channel_ = grpc_inproc_channel_create(server_, args.ToC().get(), nullptr);
  } else {
    grpc_channel_credentials* creds =
        create_channel.has_channel_creds()
            ? ReadChannelCreds(create_channel.channel_creds())
            : grpc_insecure_credentials_create();
    channel_ =
        grpc_channel_create(SanitizeTargetUri(create_channel.target()).c_str(),
                            creds, args.ToC().get());
    grpc_channel_credentials_release(creds);
  }
  GRPC_CHECK_NE(channel_, nullptr);
  channel_force_delete_ = false;
  return Result::kComplete;
}

ApiFuzzer::Result ApiFuzzer::CreateServer(
    const api_fuzzer::CreateServer& create_server) {
  if (server_ == nullptr) {
    // ExecCtx is needed for ChannelArgs destruction.
    ExecCtx exec_ctx;
    testing::FuzzingEnvironment fuzzing_env;
    fuzzing_env.resource_quota = resource_quota();
    ChannelArgs args = testing::CreateChannelArgsFromFuzzingConfiguration(
        create_server.channel_args(), fuzzing_env);
    server_ = grpc_server_create(args.ToC().get(), nullptr);
    GRPC_CHECK_NE(server_, nullptr);
    grpc_server_register_completion_queue(server_, cq(), nullptr);
    for (const auto& http2_port : create_server.http2_ports()) {
      auto* creds = ReadServerCreds(http2_port.server_creds());
      auto addr = absl::StrCat("localhost:", http2_port.port());
      grpc_server_add_http2_port(server_, addr.c_str(), creds);
      grpc_server_credentials_release(creds);
    }
    grpc_server_start(server_);
    ResetServerState();
  } else {
    return Result::kFailed;
  }
  return Result::kComplete;
}

void ApiFuzzer::DestroyServer() {
  grpc_server_destroy(server_);
  server_ = nullptr;
}

void ApiFuzzer::DestroyChannel() {
  grpc_channel_destroy(channel_);
  channel_ = nullptr;
}

void RunApiFuzzer(const api_fuzzer::Msg& msg) {
  ApplyFuzzConfigVars(msg.config_vars());
  TestOnlyReloadExperimentsFromConfigVariables();
  ApiFuzzer(msg.event_engine_actions()).Run(msg.actions());
}
FUZZ_TEST(MyTestSuite, RunApiFuzzer)
    .WithDomains(::fuzztest::Arbitrary<api_fuzzer::Msg>().WithProtobufField(
        "config_vars", AnyConfigVars()));

auto ParseTestProto(const std::string& proto) {
  api_fuzzer::Msg msg;
  GRPC_CHECK(google::protobuf::TextFormat::ParseFromString(proto, &msg));
  return msg;
}

TEST(MyTestSuite, RunApiFuzzerRegression1) {
  RunApiFuzzer(ParseTestProto(
      R"pb(actions { create_server {} }
           actions { shutdown_server {} }
           actions { create_channel { inproc: true } }
           actions {
             create_call {
               method { value: "\364\217\277\277" }
               host { value: ")" }
             }
           }
      )pb"));
}

TEST(MyTestSuite, RunApiFuzzerRegression2) {
  RunApiFuzzer(ParseTestProto(
      R"pb(actions { create_server { http2_ports { server_creds {} } } }
           actions { request_call {} }
           actions {
             create_channel {
               channel_args {}
               inproc: true
             }
           }
           actions {
             create_call {
               method { value: "\364\217\277\277\355\237\277" }
               timeout: -1482173017
             }
           }
           actions { poll_cq {} }
           actions {
             queue_batch {
               operations {
                 send_status_from_server {
                   status_code: 4294967295
                   status_details { value: "_" }
                 }
               }
             }
           }
      )pb"));
}

}  // namespace testing
}  // namespace grpc_core
