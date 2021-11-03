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

#ifndef GRPC_CORE_LIB_RESOLVER_RESOLVER_FACTORY_H
#define GRPC_CORE_LIB_RESOLVER_RESOLVER_FACTORY_H

#include <grpc/support/port_platform.h>

#include "absl/strings/strip.h"

#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/uri/uri_parser.h"

typedef struct grpc_pollset_set grpc_pollset_set;

namespace grpc_core {

// TODO(yashkt): Move WorkSerializer to its own Bazel target, depend on that
// target from this one, and remove this forward declaration.
class WorkSerializer;

struct ResolverArgs {
  /// The parsed URI to resolve.
  URI uri;
  /// The URI string.
  std::string uri_string;
  /// Channel args to be included in resolver results.
  const grpc_channel_args* args = nullptr;
  /// Used to drive I/O in the name resolution process.
  grpc_pollset_set* pollset_set = nullptr;
  /// The work_serializer under which all resolver calls will be run.
  std::shared_ptr<WorkSerializer> work_serializer;
  /// The result handler to be used by the resolver.
  std::unique_ptr<Resolver::ResultHandler> result_handler;
};

class ResolverFactory {
 public:
  /// Returns a bool indicating whether the input uri is valid to create a
  /// resolver.
  virtual bool IsValidUri(const URI& uri) const = 0;

  /// Returns a new resolver instance.
  virtual OrphanablePtr<Resolver> CreateResolver(ResolverArgs args) const = 0;

  /// Returns a string representing the default authority to use for this
  /// scheme.
  virtual std::string GetDefaultAuthority(const URI& uri) const {
    return std::string(absl::StripPrefix(uri.path(), "/"));
  }

  /// Returns the URI scheme that this factory implements.
  /// Caller does NOT take ownership of result.
  virtual const char* scheme() const = 0;

  virtual ~ResolverFactory() {}
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_RESOLVER_RESOLVER_FACTORY_H */
