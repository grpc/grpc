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
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/lib/channel/channel_args.h"

#define GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR \
  "grpc.fake_resolver.response_generator"

typedef struct {
  // base class -- must be first
  grpc_resolver base;

  // passed-in parameters
  grpc_channel_args* channel_args;

  // If not NULL, the next set of resolution results to be returned to
  // grpc_resolver_next_locked()'s closure.
  grpc_channel_args* next_results;

  // Results to use for the pretended re-resolution in
  // fake_resolver_channel_saw_error_locked().
  grpc_channel_args* results_upon_error;

  // pending next completion, or NULL
  grpc_closure* next_completion;
  // target result address for next completion
  grpc_channel_args** target_result;
} fake_resolver;

void grpc_resolver_fake_init();

// Instances of \a grpc_fake_resolver_response_generator are passed to the
// fake resolver in a channel argument (see \a
// grpc_fake_resolver_response_generator_arg) in order to inject and trigger
// custom resolutions. See also \a
// grpc_fake_resolver_response_generator_set_response.
typedef struct grpc_fake_resolver_response_generator
    grpc_fake_resolver_response_generator;
grpc_fake_resolver_response_generator*
grpc_fake_resolver_response_generator_create();

// Set the resolution response of the fake resolver associated with the \a
// response_generator instance. If \a only_upon_error is true, only set \a
// results_upon_error of the resolver. Otherwise, set both \a only_upon_error
// and \a next_results and trigger a new resolution.
void grpc_fake_resolver_response_generator_set_response(
    grpc_fake_resolver_response_generator* generator,
    grpc_channel_args* response, bool only_upon_error);

// Return a \a grpc_arg for a \a grpc_fake_resolver_response_generator instance.
grpc_arg grpc_fake_resolver_response_generator_arg(
    grpc_fake_resolver_response_generator* generator);
// Return the \a grpc_fake_resolver_response_generator instance in \a args or
// NULL.
grpc_fake_resolver_response_generator*
grpc_fake_resolver_get_response_generator(const grpc_channel_args* args);

grpc_fake_resolver_response_generator*
grpc_fake_resolver_response_generator_ref(
    grpc_fake_resolver_response_generator* generator);
void grpc_fake_resolver_response_generator_unref(
    grpc_fake_resolver_response_generator* generator);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_FAKE_FAKE_RESOLVER_H \
        */
