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

#include <memory>
#include <queue>

#include "absl/status/status.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/ext/filters/client_channel/resolver/dns/event_engine/event_engine_client_channel_resolver.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/event_engine/util/aborting_event_engine.h"
#include "test/core/ext/filters/event_engine_client_channel_resolver/resolver_ops.pb.h"

// TODO(hork): exercise Orphan on the client channel resolver, which will
// exercise the resolution cancellation path. Currently, all requests will get
// responses.

namespace {

using grpc_core::EventEngineClientChannelDNSResolverFactory;
using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::FuzzingEventEngine;

class FuzzingResolverEventEngine
    : public grpc_event_engine::experimental::AbortingEventEngine {
 public:
  FuzzingResolverEventEngine(
      const event_engine_client_channel_resolver::Msg& msg)
      : runner_(FuzzingEventEngine::Options(),
                fuzzing_event_engine::Actions()) {
    // Set hostname responses
    for (const auto& hostname_response : msg.hostname_response()) {
      if (hostname_response.has_error()) {
        hostname_responses_.emplace(absl::nullopt);
      } else if (hostname_response.has_addresses() &&
                 hostname_response.addresses().address_size() > 0) {
        // add a set of basic addresses
        std::vector<EventEngine::ResolvedAddress> resolved_addresses;
        resolved_addresses.resize(hostname_response.addresses().address_size(),
                                  EventEngine::ResolvedAddress());
        hostname_responses_.emplace(std::move(resolved_addresses));
      }
    }
    // Set SRV Responses
    for (const auto& srv_response : msg.srv_response()) {
      if (srv_response.has_error()) {
        srv_responses_.emplace(absl::nullopt);
      } else if (srv_response.has_srv_records() &&
                 srv_response.srv_records().srv_records_size() > 0) {
        std::vector<EventEngine::DNSResolver::SRVRecord> records;
        records.reserve(srv_response.srv_records().srv_records_size());
        for (const auto& r : srv_response.srv_records().srv_records()) {
          EventEngine::DNSResolver::SRVRecord final_r;
          final_r.host = r.host();
          final_r.port = r.port();
          final_r.priority = r.priority();
          final_r.weight = r.weight();
          records.push_back(final_r);
        }
        srv_responses_.emplace(std::move(records));
      }
    }
    // Set TXT Responses
    for (const auto& txt_response : msg.txt_response()) {
      if (txt_response.has_error()) {
        txt_responses_.emplace(absl::nullopt);
      } else if (txt_response.has_valid_response()) {
        txt_responses_.emplace(txt_valid_config_);
      } else if (txt_response.has_empty_response()) {
        txt_responses_.emplace("");
      } else if (txt_response.has_invalid_response()) {
        txt_responses_.emplace(txt_invalid_config_);
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
    virtual LookupTaskHandle LookupHostname(
        LookupHostnameCallback on_resolve, absl::string_view /* name */,
        absl::string_view /* default_port */, Duration /* timeout */) override {
      auto finish =
          [cb = std::move(on_resolve), runner = &engine_->runner_](
              absl::StatusOr<std::vector<ResolvedAddress>> response) mutable {
            runner->Run(
                [cb = std::move(cb), response = std::move(response)]() mutable {
                  cb(response);
                });
            return EventEngine::DNSResolver::LookupTaskHandle::kInvalid;
          };
      if (engine_->hostname_responses_.empty()) {
        return finish(engine_->lookup_hostname_response_base_case_);
      }
      auto canned_response = engine_->hostname_responses_.front();
      engine_->hostname_responses_.pop();
      if (!canned_response.has_value()) {
        return finish(engine_->lookup_hostname_response_base_case_);
      }
      return finish(*canned_response);
    }
    virtual LookupTaskHandle LookupSRV(LookupSRVCallback on_resolve,
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
      if (engine_->srv_responses_.empty()) {
        return finish(engine_->lookup_srv_response_base_case_);
      }
      auto response = engine_->srv_responses_.front();
      engine_->srv_responses_.pop();
      if (!response.has_value()) {
        return finish(engine_->lookup_srv_response_base_case_);
      }
      return finish(*response);
    }
    virtual LookupTaskHandle LookupTXT(LookupTXTCallback on_resolve,
                                       absl::string_view /* name */,
                                       Duration /* timeout */) override {
      auto finish = [cb = std::move(on_resolve), runner = &engine_->runner_](
                        absl::StatusOr<std::string> response) mutable {
        runner->Run(
            [cb = std::move(cb), response = std::move(response)]() mutable {
              cb(response);
            });
        return EventEngine::DNSResolver::LookupTaskHandle::kInvalid;
      };
      if (engine_->txt_responses_.empty()) {
        return finish(engine_->lookup_txt_response_base_case_);
      }
      auto response = engine_->txt_responses_.front();
      engine_->txt_responses_.pop();
      if (!response.has_value()) {
        return finish(engine_->lookup_txt_response_base_case_);
      }
      return finish(*response);
    }
    virtual bool CancelLookup(LookupTaskHandle handle) override {
      return false;
    }

   private:
    FuzzingResolverEventEngine* engine_;
  };

  // members
  FuzzingEventEngine runner_;

  // responses
  std::queue<absl::optional<std::vector<EventEngine::ResolvedAddress>>>
      hostname_responses_;
  std::queue<absl::optional<std::vector<EventEngine::DNSResolver::SRVRecord>>>
      srv_responses_;
  std::queue<absl::optional<std::string>> txt_responses_;

  // base cases
  const std::string txt_valid_config_ =
      "grpc_config=[{\"serviceConfig\":{\"loadBalancingPolicy\":\"round_"
      "robin\",\"methodConfig\":[{\"name\":[{\"method\":\"Foo\",\"service\":"
      "\"SimpleService\"}],\"waitForReady\":true}]}}]";
  const std::string txt_invalid_config_ = "grpc_config=[{\"foops\":{}}]";
  const absl::Status lookup_hostname_response_base_case_ =
      absl::InternalError("LookupHostnameResponseBaseCase");
  const absl::Status lookup_srv_response_base_case_ =
      absl::InternalError("LookupSRVResponseBaseCase");
  const absl::Status lookup_txt_response_base_case_ =
      absl::InternalError("LookupTXTResponseBaseCase");
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
  void ReportResult(grpc_core::Resolver::Result result) override {
    signal_->Notify();
  }

 private:
  grpc_core::Notification* signal_;
};

grpc_core::ResolverArgs ConstructResolverArgs(
    const event_engine_client_channel_resolver::Msg& msg,
    const grpc_core::ChannelArgs& channel_args,
    grpc_core::Notification* result_handler_notification,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) {
  grpc_core::ResolverArgs resolver_args;
  auto uri = grpc_core::URI::Parse("dns:localhost");
  GPR_ASSERT(uri.ok());
  resolver_args.uri = *uri;
  resolver_args.args = channel_args;
  // DO NOT SUBMIT(hork): do we need an actual pollset_set here?
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
      msg, channel_args, &result_handler_notification, work_serializer);
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
