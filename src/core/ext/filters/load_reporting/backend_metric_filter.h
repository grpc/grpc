#ifndef GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_BACKEND_METRIC_FILTER_H
#define GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_BACKEND_METRIC_FILTER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <string>

#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/load_reporting/backend_metric_data.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"

namespace grpc_core {

class BackendMetricFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<BackendMetricFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;
 private:
  absl::optional<std::string> MaybeSerializeBackendMetrics(
      BackendMetricProvider* provider) const;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_BACKEND_METRIC_FILTER_H
