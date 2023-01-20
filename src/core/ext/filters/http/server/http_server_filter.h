//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_HTTP_SERVER_HTTP_SERVER_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_HTTP_SERVER_HTTP_SERVER_FILTER_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

// Processes metadata on the server side for HTTP2 transports
class HttpServerFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<HttpServerFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  HttpServerFilter(bool surface_user_agent, bool allow_put_requests)
      : surface_user_agent_(surface_user_agent),
        allow_put_requests_(allow_put_requests) {}

  bool surface_user_agent_;
  bool allow_put_requests_;
};

}  // namespace grpc_core

// A Temporary channel arg that allows servers to accept PUT requests. DO NOT
// USE WITHOUT PERMISSION.
#define GRPC_ARG_DO_NOT_USE_UNLESS_YOU_HAVE_PERMISSION_FROM_GRPC_TEAM_ALLOW_BROKEN_PUT_REQUESTS \
  "grpc.http.do_not_use_unless_you_have_permission_from_grpc_team_allow_"                       \
  "broken_put_requests"

#endif  // GRPC_SRC_CORE_EXT_FILTERS_HTTP_SERVER_HTTP_SERVER_FILTER_H
