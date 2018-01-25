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

#define GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR \
  "grpc.fake_resolver.response_generator"

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

// Set next response of the fake resolver associated with the \a
// response_generator instance and trigger a new resolution.
void grpc_fake_resolver_response_generator_set_response(
    grpc_fake_resolver_response_generator* generator,
    grpc_channel_args* response);

// Set results_upon_error of the fake resolver associated with the \a
// response_generator instance. When fake_resolver_channel_saw_error_locked() is
// called, results_upon_error will be returned as long as it's non-NULL,
// otherwise the last value set by
// grpc_fake_resolver_response_generator_set_response() will be returned.
void grpc_fake_resolver_response_generator_set_response_upon_error(
    grpc_fake_resolver_response_generator* generator,
    grpc_channel_args* response);

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
