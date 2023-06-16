// Copyright 2023 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/event_engine_client_channel_resolver.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/event_engine/util/aborting_event_engine.h"
#include "test/core/ext/filters/event_engine_client_channel_resolver/resolver_fuzzer.pb.h"
#include "test/core/util/fuzz_config_vars.h"
#include "test/core/util/fuzzing_channel_args.h"

bool squelch = true;
static void dont_log(gpr_log_func_args* /*args*/) {}

namespace {

using event_engine_client_channel_resolver::ExecutionStep;
using event_engine_client_channel_resolver::TXTRecordType;
using grpc_core::EventEngineClientChannelDNSResolverFactory;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;
using grpc_event_engine::experimental::URIToResolvedAddress;

constexpr char g_grpc_config_prefix[] = "grpc_config=";

absl::Status ErrorToAbslStatus(
    const event_engine_client_channel_resolver::Error& error) {
  // clamp error.code() in (0, 16]
  return absl::Status(static_cast<absl::StatusCode>(error.code() % 16 + 1),
                      error.message());
}

class FuzzingResolverEventEngine
    : public grpc_event_engine::experimental::AbortingEventEngine {
 public:
  explicit FuzzingResolverEventEngine(
      const event_engine_client_channel_resolver::Msg& msg,
      bool* done_resolving)
      : runner_(FuzzingEventEngine::Options(), fuzzing_event_engine::Actions()),
        done_resolving_(done_resolving),
        should_orphan_at_step_(msg.should_orphan_at_step()) {
    // Set hostname responses
    if (msg.has_hostname_error()) {
      hostname_responses_ = ErrorToAbslStatus(msg.hostname_error());
    } else if (msg.has_hostname_response()) {
      hostname_responses_.emplace();
      for (const auto& address : msg.hostname_response().addresses()) {
        hostname_responses_->emplace_back(*URIToResolvedAddress(
            absl::StrCat("ipv4:127.0.0.1:", address.port() % 65535)));
      }
    }
    // Set SRV Responses
    if (msg.has_srv_error()) {
      srv_responses_ = ErrorToAbslStatus(msg.srv_error());
    } else if (msg.has_srv_response()) {
      srv_responses_.emplace();
      for (const auto& srv_record : msg.srv_response().srv_records()) {
        EventEngine::DNSResolver::SRVRecord final_r;
        final_r.host = srv_record.host();
        final_r.port = srv_record.port();
        final_r.priority = srv_record.priority();
        final_r.weight = srv_record.weight();
        srv_responses_->emplace_back(final_r);
      }
    }
    // Set TXT Responses
    if (msg.has_txt_error()) {
      txt_responses_ = ErrorToAbslStatus(msg.txt_error());
    } else if (msg.has_txt_response()) {
      txt_responses_.emplace();
      for (const auto& txt_record : msg.txt_response().txt_records()) {
        if (txt_record.has_enumerated_value()) {
          switch (txt_record.enumerated_value()) {
            case TXTRecordType::TXT_UNDEFINED:
              break;
            case TXTRecordType::TXT_VALID:
              txt_responses_->emplace_back(txt_valid_config_);
              break;
            case TXTRecordType::TXT_RANDOM_NON_CONFIG:
              txt_responses_->emplace_back(txt_record.arbitrary_value());
              break;
            case TXTRecordType::TXT_RANDOM_PREFIXED_CONFIG:
              txt_responses_->emplace_back(absl::StrCat(
                  g_grpc_config_prefix, txt_record.arbitrary_value()));
              break;
            default:
              //  ignored
              break;
          }
        }
      }
    }
  }

  std::unique_ptr<DNSResolver> GetDNSResolver(
      const DNSResolver::ResolverOptions& /* options */) override {
    return std::make_unique<FuzzingDNSResolver>(this);
  }

  TaskHandle RunAfter(Duration /* when */,
                      absl::AnyInvocable<void()> /* closure */) override {
    return TaskHandle::kInvalid;
  }
  bool Cancel(TaskHandle /* handle */) override { return true; }

  void Tick() { runner_.Tick(); }

 private:
  class FuzzingDNSResolver : public DNSResolver {
   public:
    explicit FuzzingDNSResolver(FuzzingResolverEventEngine* engine)
        : engine_(engine) {}

    void LookupHostname(LookupHostnameCallback on_resolve,
                        absl::string_view /* name */,
                        absl::string_view /* default_port */) override {
      CheckAndSetOrphan(ExecutionStep::DURING_LOOKUP_HOSTNAME);
      if (!engine_->has_been_orphaned_) {
        engine_->runner_.Run([this, cb = std::move(on_resolve)]() mutable {
          CheckAndSetOrphan(ExecutionStep::AFTER_LOOKUP_HOSTNAME_CALLBACK);
          cb(engine_->hostname_responses_);
        });
      }
    }
    void LookupSRV(LookupSRVCallback on_resolve,
                   absl::string_view /* name */) override {
      CheckAndSetOrphan(ExecutionStep::DURING_LOOKUP_SRV);
      if (!engine_->has_been_orphaned_) {
        engine_->runner_.Run([this, cb = std::move(on_resolve)]() mutable {
          CheckAndSetOrphan(ExecutionStep::AFTER_LOOKUP_SRV_CALLBACK);
          cb(engine_->srv_responses_);
        });
      }
    }
    void LookupTXT(LookupTXTCallback on_resolve,
                   absl::string_view /* name */) override {
      CheckAndSetOrphan(ExecutionStep::DURING_LOOKUP_TXT);
      if (!engine_->has_been_orphaned_) {
        engine_->runner_.Run([this, cb = std::move(on_resolve)]() mutable {
          CheckAndSetOrphan(ExecutionStep::AFTER_LOOKUP_TXT_CALLBACK);
          cb(engine_->txt_responses_);
        });
      }
    }

   private:
    void CheckAndSetOrphan(ExecutionStep current_execution_step) {
      if (engine_->should_orphan_at_step_ == current_execution_step) {
        *engine_->done_resolving_ = true;
        engine_->has_been_orphaned_ = true;
      }
    }

    FuzzingResolverEventEngine* engine_;
  };

  // members
  FuzzingEventEngine runner_;
  bool* done_resolving_;
  ExecutionStep should_orphan_at_step_;
  bool has_been_orphaned_ = false;

  // responses
  absl::StatusOr<std::vector<EventEngine::ResolvedAddress>> hostname_responses_;
  absl::StatusOr<std::vector<EventEngine::DNSResolver::SRVRecord>>
      srv_responses_;
  absl::StatusOr<std::vector<std::string>> txt_responses_;

  // base case for a valid gRPC config
  const std::string txt_valid_config_ =
      "grpc_config=[{\"serviceConfig\":{\"loadBalancingPolicy\":\"round_"
      "robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":"
      "\"SimpleService\"}],\"waitForReady\":true}]}}]";
};

class FuzzingResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  explicit FuzzingResultHandler(bool* done_resolving)
      : done_resolving_(done_resolving) {}
  void ReportResult(grpc_core::Resolver::Result /* result */) override {
    *done_resolving_ = true;
  }

 private:
  bool* done_resolving_;
};

grpc_core::ResolverArgs ConstructResolverArgs(
    const grpc_core::ChannelArgs& channel_args, bool* done_resolving,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) {
  grpc_core::ResolverArgs resolver_args;
  auto uri = grpc_core::URI::Parse("dns:localhost");
  GPR_ASSERT(uri.ok());
  resolver_args.uri = *uri;
  resolver_args.args = channel_args;
  resolver_args.pollset_set = nullptr;
  resolver_args.work_serializer = std::move(work_serializer);
  auto result_handler = std::make_unique<FuzzingResultHandler>(done_resolving);
  resolver_args.result_handler = std::move(result_handler);
  return resolver_args;
}

}  // namespace

DEFINE_PROTO_FUZZER(const event_engine_client_channel_resolver::Msg& msg) {
  if (squelch) gpr_set_log_function(dont_log);
  bool done_resolving = false;
  grpc_core::ApplyFuzzConfigVars(msg.config_vars());
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  grpc_event_engine::experimental::SetEventEngineFactory([msg,
                                                          &done_resolving]() {
    return std::make_unique<FuzzingResolverEventEngine>(msg, &done_resolving);
  });
  auto engine = std::static_pointer_cast<FuzzingResolverEventEngine>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  {
    // scoped to ensure the resolver is orphaned when done resolving.
    auto work_serializer = std::make_shared<grpc_core::WorkSerializer>();
    EventEngineClientChannelDNSResolverFactory resolver_factory;
    auto resolver_args = ConstructResolverArgs(
        grpc_core::testing::CreateChannelArgsFromFuzzingConfiguration(
            msg.channel_args(), {})
            .Set(GRPC_INTERNAL_ARG_EVENT_ENGINE, engine),
        &done_resolving, work_serializer);
    auto resolver = resolver_factory.CreateResolver(std::move(resolver_args));
    work_serializer->Run(
        [resolver_ptr = resolver.get()]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
            *work_serializer) { resolver_ptr->StartLocked(); },
        DEBUG_LOCATION);
    // wait for result (no need to check validity)
    while (!done_resolving) {
      engine->Tick();
    }
  }
  // If orphaned early, callbacks may still need to run, which may keep the
  // resolver alive.
  while (engine.use_count() > 1) engine->Tick();
}
