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
#include "absl/time/time.h"
#include "google/protobuf/json/json.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/event_engine_client_channel_resolver.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "src/proto/grpc/service_config/service_config.pb.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/event_engine/util/aborting_event_engine.h"
#include "test/core/ext/filters/event_engine_client_channel_resolver/resolver_ops.pb.h"

// TODO(hork): exercise Orphan on the client channel resolver, which will
// exercise the resolution cancellation path. Currently, all requests will get
// responses.

bool squelch = true;

namespace {

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
      const event_engine_client_channel_resolver::Msg& msg)
      : runner_(FuzzingEventEngine::Options(),
                fuzzing_event_engine::Actions()) {
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
              grpc_core::Crash("Invalid txt record type");
          }
        } else if (txt_record.has_fuzzed_service_config()) {
          std::string fuzzed_config_json_str;
          ::google::protobuf::json::PrintOptions print_options;
          auto status =
              MessageToJsonString(txt_record.fuzzed_service_config(),
                                  &fuzzed_config_json_str, print_options);
          // Sometimes LLVM will generate protos that can't be dumped to JSON
          // (Durations out of bounds, for example). These are ignored.
          if (status.ok()) {
            txt_responses_->emplace_back(fuzzed_config_json_str);
          }
        }
      }
    }
  }
  std::unique_ptr<DNSResolver> GetDNSResolver(
      const DNSResolver::ResolverOptions& /* options */) override {
    return std::make_unique<FuzzingDNSResolver>(this);
  }

  void Tick() { runner_.Tick(); }

 private:
  class FuzzingDNSResolver : public DNSResolver {
   public:
    explicit FuzzingDNSResolver(FuzzingResolverEventEngine* engine)
        : engine_(engine) {}
    LookupTaskHandle LookupHostname(LookupHostnameCallback on_resolve,
                                    absl::string_view /* name */,
                                    absl::string_view /* default_port */,
                                    Duration /* timeout */) override {
      auto finish =
          [cb = std::move(on_resolve), runner = &engine_->runner_](
              absl::StatusOr<std::vector<ResolvedAddress>> response) mutable {
            runner->Run(
                [cb = std::move(cb), response = std::move(response)]() mutable {
                  cb(response);
                });
            return EventEngine::DNSResolver::LookupTaskHandle::kInvalid;
          };
      return finish(engine_->hostname_responses_);
    }
    LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
                               absl::string_view /* name */,
                               Duration /* timeout */) override {
      auto finish =
          [cb = std::move(on_resolve), runner = &engine_->runner_](
              absl::StatusOr<std::vector<SRVRecord>> response) mutable {
            runner->Run(
                [cb = std::move(cb), response = std::move(response)]() mutable {
                  cb(response);
                });
            return EventEngine::DNSResolver::LookupTaskHandle::kInvalid;
          };
      return finish(engine_->srv_responses_);
    }
    LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                               absl::string_view /* name */,
                               Duration /* timeout */) override {
      auto finish =
          [cb = std::move(on_resolve), runner = &engine_->runner_](
              absl::StatusOr<std::vector<std::string>> response) mutable {
            runner->Run(
                [cb = std::move(cb), response = std::move(response)]() mutable {
                  cb(response);
                });
            return EventEngine::DNSResolver::LookupTaskHandle::kInvalid;
          };
      return finish(engine_->txt_responses_);
    }
    bool CancelLookup(LookupTaskHandle /* handle */) override { return false; }

   private:
    FuzzingResolverEventEngine* engine_;
  };

  // members
  FuzzingEventEngine runner_;

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

grpc_core::ChannelArgs ConstructChannelArgs(
    const event_engine_client_channel_resolver::Msg& msg,
    std::shared_ptr<FuzzingResolverEventEngine> engine) {
  // put the engine in channel args
  return grpc_core::ChannelArgs()
      .SetObject<EventEngine>(std::move(engine))
      .Set(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION,
           !msg.resolver_args().request_service_config())
      .Set(GRPC_ARG_DNS_ENABLE_SRV_QUERIES,
           msg.resolver_args().enable_srv_queries());
}

class FuzzingResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  explicit FuzzingResultHandler(grpc_core::Notification* signal)
      : signal_(signal) {}
  void ReportResult(grpc_core::Resolver::Result /* result */) override {
    signal_->Notify();
  }

 private:
  grpc_core::Notification* signal_;
};

grpc_core::ResolverArgs ConstructResolverArgs(
    const grpc_core::ChannelArgs& channel_args,
    grpc_core::Notification* result_handler_notification,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) {
  grpc_core::ResolverArgs resolver_args;
  auto uri = grpc_core::URI::Parse("dns:localhost");
  GPR_ASSERT(uri.ok());
  resolver_args.uri = *uri;
  resolver_args.args = channel_args;
  resolver_args.pollset_set = nullptr;
  resolver_args.work_serializer = std::move(work_serializer);
  auto result_handler =
      std::make_unique<FuzzingResultHandler>(result_handler_notification);
  resolver_args.result_handler = std::move(result_handler);
  return resolver_args;
}

}  // namespace

DEFINE_PROTO_FUZZER(const event_engine_client_channel_resolver::Msg& msg) {
  auto engine = std::make_shared<FuzzingResolverEventEngine>(msg);
  auto channel_args = ConstructChannelArgs(msg, engine);
  grpc_core::Notification result_handler_notification;
  auto work_serializer = std::make_shared<grpc_core::WorkSerializer>();
  auto resolver_args = ConstructResolverArgs(
      channel_args, &result_handler_notification, work_serializer);
  EventEngineClientChannelDNSResolverFactory resolver_factory;
  auto resolver = resolver_factory.CreateResolver(std::move(resolver_args));
  work_serializer->Run([resolver = resolver.get()]()
                           ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                               *work_serializer) { resolver->StartLocked(); },
                       DEBUG_LOCATION);
  // wait for result (no need to check validity)
  do {
    engine->Tick();
  } while (!result_handler_notification.WaitForNotificationWithTimeout(
      absl::Milliseconds(33)));
}
