//
// Copyright 2025 gRPC authors.
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

#include "src/core/filter/composite/composite_filter.h"

namespace grpc_core {

const grpc_channel_filter CompositeFilter::kFilterVtable =
    MakePromiseBasedFilter<StatefulSessionFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata|kFilterExaminesOutboundMessages|kFilterExaminesInboundMessages|kFilterExaminesCallContext>();

absl::StatusOr<std::unique_ptr<CompositeFilter>> CompositeFilter::Create(
    const ChannelArgs& args, ChannelFilter::Args filter_args) {
  // FIXME: instantiate
}

CompositeFilter::CompositeFilter(std::unique_ptr<const Config> config)
    : config_(std::move(config)) {
  // FIXME: populate filter_chain_map_ from config_
}

}  // namespace grpc_core
