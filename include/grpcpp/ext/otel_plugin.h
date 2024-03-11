//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPCPP_EXT_OTEL_PLUGIN_H
#define GRPCPP_EXT_OTEL_PLUGIN_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/metrics/meter_provider.h"

namespace grpc {

namespace internal {
class OpenTelemetryPluginBuilderImpl;
}  // namespace internal

class OpenTelemetryPluginOption {
 public:
  virtual ~OpenTelemetryPluginOption() = default;
};

/// The most common way to use this API is -
///
/// OpenTelemetryPluginBuilder().SetMeterProvider(provider).BuildAndRegister();
///
/// The set of instruments available are -
/// grpc.client.attempt.started
/// grpc.client.attempt.duration
/// grpc.client.attempt.sent_total_compressed_message_size
/// grpc.client.attempt.rcvd_total_compressed_message_size
/// grpc.server.call.started
/// grpc.server.call.duration
/// grpc.server.call.sent_total_compressed_message_size
/// grpc.server.call.rcvd_total_compressed_message_size
class OpenTelemetryPluginBuilder {
 public:
  /// Metrics
  static constexpr absl::string_view kClientAttemptStartedInstrumentName =
      "grpc.client.attempt.started";
  static constexpr absl::string_view kClientAttemptDurationInstrumentName =
      "grpc.client.attempt.duration";
  static constexpr absl::string_view
      kClientAttemptSentTotalCompressedMessageSizeInstrumentName =
          "grpc.client.attempt.sent_total_compressed_message_size";
  static constexpr absl::string_view
      kClientAttemptRcvdTotalCompressedMessageSizeInstrumentName =
          "grpc.client.attempt.rcvd_total_compressed_message_size";
  static constexpr absl::string_view kServerCallStartedInstrumentName =
      "grpc.server.call.started";
  static constexpr absl::string_view kServerCallDurationInstrumentName =
      "grpc.server.call.duration";
  static constexpr absl::string_view
      kServerCallSentTotalCompressedMessageSizeInstrumentName =
          "grpc.server.call.sent_total_compressed_message_size";
  static constexpr absl::string_view
      kServerCallRcvdTotalCompressedMessageSizeInstrumentName =
          "grpc.server.call.rcvd_total_compressed_message_size";

  OpenTelemetryPluginBuilder();
  ~OpenTelemetryPluginBuilder();
  /// If `SetMeterProvider()` is not called, no metrics are collected.
  OpenTelemetryPluginBuilder& SetMeterProvider(
      std::shared_ptr<opentelemetry::metrics::MeterProvider> meter_provider);
  /// If set, \a target_attribute_filter is called per channel to decide whether
  /// to record the target attribute on client or to replace it with "other".
  /// This helps reduce the cardinality on metrics in cases where many channels
  /// are created with different targets in the same binary (which might happen
  /// for example, if the channel target string uses IP addresses directly).
  OpenTelemetryPluginBuilder& SetTargetAttributeFilter(
      absl::AnyInvocable<bool(absl::string_view /*target*/) const>
          target_attribute_filter);
  /// If set, \a generic_method_attribute_filter is called per call with a
  /// generic method type to decide whether to record the method name or to
  /// replace it with "other". Non-generic or pre-registered methods remain
  /// unaffected. If not set, by default, generic method names are replaced with
  /// "other" when recording metrics.
  OpenTelemetryPluginBuilder& SetGenericMethodAttributeFilter(
      absl::AnyInvocable<bool(absl::string_view /*generic_method*/) const>
          generic_method_attribute_filter);
  /// Add a plugin option to add to the opentelemetry plugin being built. At
  /// present, this type is an opaque type. Ownership of \a option is
  /// transferred when `AddPluginOption` is invoked. A maximum of 64 plugin
  /// options can be added.
  OpenTelemetryPluginBuilder& AddPluginOption(
      std::unique_ptr<OpenTelemetryPluginOption> option);
  /// Registers a global plugin that acts on all channels and servers running on
  /// the process.
  absl::Status BuildAndRegisterGlobal();

 private:
  std::unique_ptr<internal::OpenTelemetryPluginBuilderImpl> impl_;
};

namespace experimental {
// TODO(yashykt): Delete this after the 1.62 release.
GRPC_DEPRECATED(
    "Use grpc::OpenTelemetryPluginBuilder instead. The experimental version "
    "will be deleted after the 1.62 release.")
typedef grpc::OpenTelemetryPluginBuilder OpenTelemetryPluginBuilder;
}  // namespace experimental

}  // namespace grpc

#endif  // GRPCPP_EXT_OTEL_PLUGIN_H
