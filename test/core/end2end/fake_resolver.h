//
// Copyright 2016, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#ifndef GRPC_TEST_CORE_END2END_FAKE_RESOLVER_H
#define GRPC_TEST_CORE_END2END_FAKE_RESOLVER_H

#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/util/test_config.h"

void grpc_fake_resolver_init();

// Return a \a grpc_arg enabling LB.
grpc_arg grpc_fake_resolver_lb_enabled_arg();

// Return a \a grpc_arg setting \a balancer_names a (comma-separated) string of
// names for the load balancers.
grpc_arg grpc_fake_resolver_balancer_names_arg(char* balancer_names);

// Return whether LB is enabled in \a args.
bool grpc_fake_resolver_get_lb_enabled(const grpc_channel_args* args);

// Return the comma-separated string of balancer names in \a args or NULL.
const char* grpc_fake_resolver_get_balancer_names(
    const grpc_channel_args* args);

// Instances of \a grpc_fake_resolver_response_generator are passed to the
// fake resolver in a channel argument (see \a
// grpc_fake_resolver_response_generator_arg) in order to inject and trigger
// custom resolutions. See also \a
// grpc_fake_resolver_response_generator_set_response.
typedef struct grpc_fake_resolver_response_generator
    grpc_fake_resolver_response_generator;
grpc_fake_resolver_response_generator*
grpc_fake_resolver_response_generator_create();

// Instruct the fake resolver associated with the \a response_generator instance
// to trigger a new resolution for \a uri and \a args.
void grpc_fake_resolver_response_generator_set_response(
    grpc_exec_ctx* exec_ctx,
    const grpc_fake_resolver_response_generator* response_generator,
    const grpc_uri* uri, const grpc_channel_args* args);

grpc_fake_resolver_response_generator*
grpc_fake_resolver_response_generator_ref(
    grpc_fake_resolver_response_generator* generator);

void grpc_fake_resolver_response_generator_unref(
    grpc_fake_resolver_response_generator* generator);

// Return a \a grpc_arg for a \a grpc_fake_resolver_response_generator instance.
grpc_arg grpc_fake_resolver_response_generator_arg();

// Return the \a grpc_fake_resolver_response_generator instance in \a args or
// NULL.
grpc_fake_resolver_response_generator*
grpc_fake_resolver_get_response_generator(const grpc_channel_args* args);

#endif /* GRPC_TEST_CORE_END2END_FAKE_RESOLVER_H */
