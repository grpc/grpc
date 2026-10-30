// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_FILTER_FILTER_ARGS_H
#define GRPC_SRC_CORE_FILTER_FILTER_ARGS_H

#include <memory>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/util/match.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"

// A boolean channel arg indicating whether we're using the v3 filter stack.
#define GRPC_ARG_USE_V3_STACK "grpc.internal.use_v3_stack"

// A boolean channel arg indicating whether the filter stack is for the
// server side.
#define GRPC_ARG_IS_SERVER_FILTER_STACK "grpc.internal.is_server_filter_stack"

namespace grpc_core {

// A base class for filter configs.
class FilterConfig : public RefCounted<FilterConfig> {
 public:
  virtual UniqueTypeName type() const = 0;

  bool operator==(const FilterConfig& other) const {
    if (type() != other.type()) return false;
    return Equals(other);
  }

  bool operator!=(const FilterConfig& other) const { return !(*this == other); }

  virtual bool Equals(const FilterConfig& other) const = 0;

  virtual std::string ToString() const = 0;
};

struct FilterAndConfig {
  const grpc_channel_filter* filter;
  RefCountedPtr<const FilterConfig> config;
};

// Filter arguments that are independent of channel args.
// Here-in should be things that depend on the filters location in the stack, or
// things that are ephemeral and disjoint from overall channel args.
class FilterArgs {
 public:
  explicit FilterArgs(grpc_channel_stack* channel_stack = nullptr,
                      RefCountedPtr<const FilterConfig> config = nullptr)
      : channel_stack_(channel_stack), config_(std::move(config)) {}

  ABSL_DEPRECATED("Direct access to channel stack is deprecated")
  grpc_channel_stack* channel_stack() const { return channel_stack_; }

  RefCountedPtr<const FilterConfig> config() const { return config_; }

 private:
  grpc_channel_stack* channel_stack_;
  const RefCountedPtr<const FilterConfig> config_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_FILTER_ARGS_H
