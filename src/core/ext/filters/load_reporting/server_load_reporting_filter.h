/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_FILTER_H
#define GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_FILTER_H

#include <grpc/support/port_platform.h>

#include <string>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/promise_based_filter.h"

namespace grpc_core {

class ServerLoadReportingFilter : public ChannelFilter {
 public:
  static absl::StatusOr<ServerLoadReportingFilter> Create(ChannelArgs args,
                                                          ChannelFilter::Args);

  // Getters.
  const char* peer_identity() { return peer_identity_.c_str(); }
  size_t peer_identity_len() { return peer_identity_.length(); }

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  // The peer's authenticated identity.
  std::string peer_identity_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_LOAD_REPORTING_SERVER_LOAD_REPORTING_FILTER_H
