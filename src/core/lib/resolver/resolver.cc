/*
 *
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/resolver/resolver.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_resolver_refcount(false,
                                                           "resolver_refcount");

namespace grpc_core {

//
// Resolver
//

Resolver::Resolver()
    : InternallyRefCounted(GRPC_TRACE_FLAG_ENABLED(grpc_trace_resolver_refcount)
                               ? "Resolver"
                               : nullptr) {}

//
// Resolver::Result
//

Resolver::Result::~Result() { grpc_channel_args_destroy(args); }

Resolver::Result::Result(const Result& other)
    : addresses(other.addresses),
      service_config(other.service_config),
      resolution_note(other.resolution_note),
      args(grpc_channel_args_copy(other.args)) {}

Resolver::Result::Result(Result&& other) noexcept
    : addresses(std::move(other.addresses)),
      service_config(std::move(other.service_config)),
      resolution_note(std::move(other.resolution_note)),
      // TODO(roth): Use std::move() once channel args is converted to C++.
      args(other.args) {
  other.args = nullptr;
}

Resolver::Result& Resolver::Result::operator=(const Result& other) {
  if (&other == this) return *this;
  addresses = other.addresses;
  service_config = other.service_config;
  resolution_note = other.resolution_note;
  grpc_channel_args_destroy(args);
  args = grpc_channel_args_copy(other.args);
  return *this;
}

Resolver::Result& Resolver::Result::operator=(Result&& other) noexcept {
  addresses = std::move(other.addresses);
  service_config = std::move(other.service_config);
  resolution_note = std::move(other.resolution_note);
  // TODO(roth): Use std::move() once channel args is converted to C++.
  grpc_channel_args_destroy(args);
  args = other.args;
  other.args = nullptr;
  return *this;
}

}  // namespace grpc_core
