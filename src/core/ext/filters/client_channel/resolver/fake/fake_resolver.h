//
// Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_FAKE_FAKE_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_FAKE_FAKE_RESOLVER_H

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/support/ref_counted.h"

#define GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR \
  "grpc.fake_resolver.response_generator"

namespace grpc_core {

class FakeResolver;

// Instances of \a grpc_fake_resolver_response_generator are passed to the
// fake resolver in a channel argument (see \a MakeChannelArg) in order to
// inject and trigger custom resolutions.
class FakeResolverResponseGenerator : public RefCounted {
 public:
  FakeResolverResponseGenerator() {}

  // Instructs the fake resolver associated with the response generator
  // instance to trigger a new resolution with the specified response.
  void SetResponse(grpc_channel_args* next_response);

  // Returns a channel arg containing \a generator.
  static grpc_arg MakeChannelArg(FakeResolverResponseGenerator* generator);

  // Returns the response generator in \a args, or null if not found.
  static FakeResolverResponseGenerator* GetFromArgs(
      const grpc_channel_args* args);

 private:
  friend class FakeResolver;

  static void SetResponseLocked(void* arg, grpc_error* error);

  FakeResolver* resolver_ = nullptr;  // Do not own.
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_FAKE_FAKE_RESOLVER_H \
        */
