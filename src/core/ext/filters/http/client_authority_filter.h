/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_HTTP_CLIENT_AUTHORITY_FILTER_H
#define GRPC_CORE_EXT_FILTERS_HTTP_CLIENT_AUTHORITY_FILTER_H

#include <grpc/support/port_platform.h>

#include "absl/status/statusor.h"

#include <grpc/impl/codegen/compression_types.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

class ClientAuthorityFilter final : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientAuthorityFilter> Create(ChannelArgs args,
                                                      ChannelFilter::Args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  explicit ClientAuthorityFilter(Slice default_authority)
      : default_authority_(std::move(default_authority)) {}
  Slice default_authority_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_HTTP_CLIENT_AUTHORITY_FILTER_H */
