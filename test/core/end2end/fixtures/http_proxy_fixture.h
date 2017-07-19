/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_HTTP_PROXY_FIXTURE_H
#define GRPC_TEST_CORE_END2END_FIXTURES_HTTP_PROXY_FIXTURE_H

#include <grpc/grpc.h>

/* The test credentials being used for HTTP Proxy Authorization */
#define GRPC_END2END_HTTP_PROXY_TEST_CONNECT_CRED "aladdin:opensesame"

/* A channel arg key used to indicate that the channel uses proxy authorization.
 * The value is of no consequence as just the presence of the argument is
 * enough. It is currently kept as of type integer but can be changed as seen
 * fit.
 */
#define GRPC_END2END_HTTP_PROXY_TEST_CONNECT_AUTH_PRESENT \
    "grpc.test.connect_auth"

typedef struct grpc_end2end_http_proxy grpc_end2end_http_proxy;

grpc_end2end_http_proxy* grpc_end2end_http_proxy_create(
    grpc_channel_args *args);

void grpc_end2end_http_proxy_destroy(grpc_end2end_http_proxy* proxy);

const char* grpc_end2end_http_proxy_get_proxy_name(
    grpc_end2end_http_proxy* proxy);

#endif /* GRPC_TEST_CORE_END2END_FIXTURES_HTTP_PROXY_FIXTURE_H */
