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

#ifndef GRPC_TEST_CORE_UTIL_PORT_SERVER_CLIENT_H
#define GRPC_TEST_CORE_UTIL_PORT_SERVER_CLIENT_H

// C interface to port_server.py

// must be synchronized with tools/run_tests/python_utils/start_port_server.py
#define GRPC_PORT_SERVER_ADDRESS "localhost:32766"

int grpc_pick_port_using_server(void);
void grpc_free_port_using_server(int port);

#endif  // GRPC_TEST_CORE_UTIL_PORT_SERVER_CLIENT_H
