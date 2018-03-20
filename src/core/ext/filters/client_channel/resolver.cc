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

#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/lib/iomgr/combiner.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_resolver_refcount(false,
                                                           "resolver_refcount");

namespace grpc_core {

Resolver::Resolver(grpc_combiner* combiner)
    : InternallyRefCountedWithTracing(&grpc_trace_resolver_refcount),
      combiner_(GRPC_COMBINER_REF(combiner, "resolver")) {}

Resolver::~Resolver() { GRPC_COMBINER_UNREF(combiner_, "resolver"); }

}  // namespace grpc_core
