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

#include "test/core/end2end/end2end_tests.h"

#include <grpc/grpc_security.h>

#include "src/core/lib/surface/channel.h"

struct grpc_end2end_local_fullstack_fixture_data {
  std::string localaddr;
};

/* Utility functions shared by h2_local tests. */
grpc_end2end_test_fixture grpc_end2end_local_chttp2_create_fixture_fullstack();

void grpc_end2end_local_chttp2_init_client_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* client_args,
    grpc_local_connect_type type);

void grpc_end2end_local_chttp2_init_server_fullstack(
    grpc_end2end_test_fixture* f, grpc_channel_args* server_args,
    grpc_local_connect_type type);

void grpc_end2end_local_chttp2_tear_down_fullstack(
    grpc_end2end_test_fixture* f);
