/*
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_HTTP_CLIENT_HTTP_CLIENT_FILTER_H
#define GRPC_CORE_EXT_FILTERS_HTTP_CLIENT_HTTP_CLIENT_FILTER_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

class HttpClientFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<HttpClientFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  HttpClientFilter(HttpSchemeMetadata::ValueType scheme, Slice user_agent,
                   bool test_only_use_put_requests);

  HttpSchemeMetadata::ValueType scheme_;
  Slice user_agent_;
  bool test_only_use_put_requests_;
};

// A test-only channel arg to allow testing gRPC Core server behavior on PUT
// requests.
#define GRPC_ARG_TEST_ONLY_USE_PUT_REQUESTS "grpc.testing.use_put_requests"

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_HTTP_CLIENT_HTTP_CLIENT_FILTER_H */
