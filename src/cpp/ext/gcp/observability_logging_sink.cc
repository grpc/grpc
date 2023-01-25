//
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
//

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/gcp/observability_logging_sink.h"

#include <algorithm>
#include <map>
#include <utility>

#include <google/protobuf/timestamp.pb.h>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"
#include "google/api/monitored_resource.pb.h"
#include "google/logging/v2/log_entry.pb.h"
#include "google/logging/v2/logging.grpc.pb.h"
#include "google/logging/v2/logging.pb.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>
#include <grpcpp/support/status.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/json/json.h"
#include "src/cpp/ext/filters/census/open_census_call_tracer.h"

namespace grpc {
namespace internal {

ObservabilityLoggingSink::ObservabilityLoggingSink(
    GcpObservabilityConfig::CloudLogging logging_config, std::string project_id,
    std::map<std::string, std::string> labels)
    : project_id_(std::move(project_id)),
      labels_(labels.begin(), labels.end()) {
  for (auto& client_rpc_event_config : logging_config.client_rpc_events) {
    client_configs_.emplace_back(client_rpc_event_config);
  }
  for (auto& server_rpc_event_config : logging_config.server_rpc_events) {
    server_configs_.emplace_back(server_rpc_event_config);
  }
  absl::optional<std::string> authority_env =
      grpc_core::GetEnv("GOOGLE_CLOUD_CPP_LOGGING_SERVICE_V2_ENDPOINT");
  absl::optional<std::string> endpoint_env =
      grpc_core::GetEnv("GOOGLE_CLOUD_CPP_LOGGING_SERVICE_V2_ENDPOINT");
  if (authority_env.has_value() && !authority_env->empty()) {
    authority_ = std::move(*endpoint_env);
  }
}

LoggingSink::Config ObservabilityLoggingSink::FindMatch(
    bool is_client, absl::string_view service, absl::string_view method) {
  const auto& configs = is_client ? client_configs_ : server_configs_;
  if (service.empty() || method.empty()) {
    return LoggingSink::Config();
  }
  for (const auto& config : configs) {
    for (const auto& config_method : config.parsed_methods) {
      if ((config_method.service == "*") ||
          ((service == config_method.service) &&
           ((config_method.method == "*") ||
            (method == config_method.method)))) {
        if (config.exclude) {
          return LoggingSink::Config();
        }
        return LoggingSink::Config(config.max_metadata_bytes,
                                   config.max_message_bytes);
      }
    }
  }
  return LoggingSink::Config();
}

namespace {

std::string EventTypeToString(LoggingSink::Entry::EventType type) {
  switch (type) {
    case LoggingSink::Entry::EventType::kClientHeader:
      return "CLIENT_HEADER";
    case LoggingSink::Entry::EventType::kServerHeader:
      return "SERVER_HEADER";
    case LoggingSink::Entry::EventType::kClientMessage:
      return "CLIENT_MESSAGE";
    case LoggingSink::Entry::EventType::kServerMessage:
      return "SERVER_MESSAGE";
    case LoggingSink::Entry::EventType::kClientHalfClose:
      return "CLIENT_HALF_CLOSE";
    case LoggingSink::Entry::EventType::kServerTrailer:
      return "SERVER_TRAILER";
    case LoggingSink::Entry::EventType::kCancel:
      return "CANCEL";
    case LoggingSink::Entry::EventType::kUnkown:
    default:
      return "EVENT_TYPE_UNKNOWN";
  }
}

std::string LoggerToString(LoggingSink::Entry::Logger type) {
  switch (type) {
    case LoggingSink::Entry::Logger::kClient:
      return "CLIENT";
    case LoggingSink::Entry::Logger::kServer:
      return "SERVER";
    case LoggingSink::Entry::Logger::kUnkown:
    default:
      return "LOGGER_UNKNOWN";
  }
}

void PayloadToJsonStructProto(LoggingSink::Entry::Payload payload,
                              ::google::protobuf::Struct* payload_proto) {
  grpc_core::Json::Object payload_json;
  if (!payload.metadata.empty()) {
    auto* metadata_proto =
        (*payload_proto->mutable_fields())["metadata"].mutable_struct_value();
    for (auto& metadata : payload.metadata) {
      if (absl::EndsWith(metadata.first, "-bin")) {
        (*metadata_proto->mutable_fields())[metadata.first].set_string_value(
            absl::WebSafeBase64Escape(metadata.second));
      } else {
        (*metadata_proto->mutable_fields())[metadata.first].set_string_value(
            std::move(metadata.second));
      }
    }
  }
  if (payload.timeout != grpc_core::Duration::Zero()) {
    (*payload_proto->mutable_fields())["timeout"].set_string_value(
        payload.timeout.ToJsonString());
  }
  if (payload.status_code != 0) {
    (*payload_proto->mutable_fields())["statusCode"].set_number_value(
        payload.status_code);
  }
  if (!payload.status_message.empty()) {
    (*payload_proto->mutable_fields())["statusMessage"].set_string_value(
        std::move(payload.status_message));
  }
  if (!payload.status_details.empty()) {
    (*payload_proto->mutable_fields())["statusDetails"].set_string_value(
        absl::Base64Escape(payload.status_details));
  }
  if (payload.message_length != 0) {
    (*payload_proto->mutable_fields())["messageLength"].set_number_value(
        payload.message_length);
  }
  if (!payload.message.empty()) {
    (*payload_proto->mutable_fields())["message"].set_string_value(
        absl::Base64Escape(payload.message));
  }
}

std::string AddressTypeToString(LoggingSink::Entry::Address::Type type) {
  switch (type) {
    case LoggingSink::Entry::Address::Type::kIpv4:
      return "TYPE_IPV4";
    case LoggingSink::Entry::Address::Type::kIpv6:
      return "TYPE_IPV6";
    case LoggingSink::Entry::Address::Type::kUnix:
      return "TYPE_UNIX";
    case LoggingSink::Entry::Address::Type::kUnknown:
    default:
      return "TYPE_UNKNOWN";
  }
}

void PeerToJsonStructProto(LoggingSink::Entry::Address peer,
                           ::google::protobuf::Struct* peer_json) {
  (*peer_json->mutable_fields())["type"].set_string_value(
      AddressTypeToString(peer.type));
  if (peer.type != LoggingSink::Entry::Address::Type::kUnknown) {
    (*peer_json->mutable_fields())["address"].set_string_value(
        std::move(peer.address));
    (*peer_json->mutable_fields())["ipPort"].set_number_value(peer.ip_port);
  }
}

}  // namespace

void EntryToJsonStructProto(LoggingSink::Entry entry,
                            ::google::protobuf::Struct* json_payload) {
  (*json_payload->mutable_fields())["callId"].set_string_value(
      absl::StrCat(entry.call_id));
  (*json_payload->mutable_fields())["sequenceId"].set_number_value(
      entry.sequence_id);
  (*json_payload->mutable_fields())["type"].set_string_value(
      EventTypeToString(entry.type));
  (*json_payload->mutable_fields())["logger"].set_string_value(
      LoggerToString(entry.logger));
  PayloadToJsonStructProto(
      std::move(entry.payload),
      (*json_payload->mutable_fields())["payload"].mutable_struct_value());
  if (entry.payload_truncated) {
    (*json_payload->mutable_fields())["payloadTruncated"].set_bool_value(
        entry.payload_truncated);
  }
  PeerToJsonStructProto(
      std::move(entry.peer),
      (*json_payload->mutable_fields())["peer"].mutable_struct_value());
  (*json_payload->mutable_fields())["authority"].set_string_value(
      std::move(entry.authority));
  (*json_payload->mutable_fields())["serviceName"].set_string_value(
      std::move(entry.service_name));
  (*json_payload->mutable_fields())["methodName"].set_string_value(
      std::move(entry.method_name));
}

void ObservabilityLoggingSink::LogEntry(Entry entry) {
  absl::call_once(once_, [this]() {
    std::string endpoint;
    absl::optional<std::string> endpoint_env =
        grpc_core::GetEnv("GOOGLE_CLOUD_CPP_LOGGING_SERVICE_V2_ENDPOINT");
    if (endpoint_env.has_value() && !endpoint_env->empty()) {
      endpoint = std::move(*endpoint_env);
    } else {
      endpoint = "logging.googleapis.com";
    }
    ChannelArguments args;
    // Disable observability for RPCs on this channel
    args.SetInt(GRPC_ARG_ENABLE_OBSERVABILITY, 0);
    // Set keepalive time to 24 hrs to effectively disable keepalive ping, but
    // still enable KEEPALIVE_TIMEOUT to get the TCP_USER_TIMEOUT effect.
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 24 * 60 * 60 * 1000 /* 24 hours */);
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 20 * 1000 /* 20 seconds */);
    stub_ = google::logging::v2::LoggingServiceV2::NewStub(
        CreateCustomChannel(endpoint, GoogleDefaultCredentials(), args));
  });
  struct CallContext {
    ClientContext context;
    google::logging::v2::WriteLogEntriesRequest request;
    google::logging::v2::WriteLogEntriesResponse response;
  };
  // TODO(yashykt): Implement batching so that we can batch a bunch of log
  // entries into a single entry. Also, set a reasonable deadline on the
  // context, and actually use the entry.
  CallContext* call = new CallContext;
  call->context.set_authority(authority_);
  call->request.set_log_name(
      absl::StrFormat("projects/%s/logs/"
                      "microservices.googleapis.com%%2Fobservability%%2fgrpc",
                      project_id_));
  (*call->request.mutable_labels()).insert(labels_.begin(), labels_.end());
  // TODO(yashykt): Figure out the proper resource type and labels.
  call->request.mutable_resource()->set_type("global");
  auto* proto_entry = call->request.add_entries();
  // Fill the current timestamp
  gpr_timespec timespec =
      grpc_core::Timestamp::Now().as_timespec(GPR_CLOCK_REALTIME);
  proto_entry->mutable_timestamp()->set_seconds(timespec.tv_sec);
  proto_entry->mutable_timestamp()->set_nanos(timespec.tv_nsec);
  // TODO(yashykt): Check if we need to fill receive timestamp
  EntryToJsonStructProto(std::move(entry), proto_entry->mutable_json_payload());
  stub_->async()->WriteLogEntries(
      &(call->context), &(call->request), &(call->response),
      [call](Status status) {
        if (!status.ok()) {
          // TODO(yashykt): Log the contents of the
          // request on a failure.
          gpr_log(GPR_ERROR, "GCP Observability Logging Error %d: %s",
                  status.error_code(), status.error_message().c_str());
        }
        delete call;
      });
}

ObservabilityLoggingSink::Configuration::Configuration(
    const GcpObservabilityConfig::CloudLogging::RpcEventConfiguration&
        rpc_event_config)
    : exclude(rpc_event_config.exclude),
      max_metadata_bytes(rpc_event_config.max_metadata_bytes),
      max_message_bytes(rpc_event_config.max_message_bytes) {
  for (auto& parsed_method : rpc_event_config.parsed_methods) {
    parsed_methods.emplace_back(ParsedMethod{
        std::string(parsed_method.service), std::string(parsed_method.method)});
  }
}

}  // namespace internal
}  // namespace grpc
