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

#ifndef GRPC_CORE_EXT_CENSUS_RPC_METRIC_ID_H
#define GRPC_CORE_EXT_CENSUS_RPC_METRIC_ID_H

/* Metric ID's used for RPC measurements. */
/* Count of client requests sent. */
#define CENSUS_METRIC_RPC_CLIENT_REQUESTS ((uint32_t)0)
/* Count of server requests sent. */
#define CENSUS_METRIC_RPC_SERVER_REQUESTS ((uint32_t)1)
/* Client error counts. */
#define CENSUS_METRIC_RPC_CLIENT_ERRORS ((uint32_t)2)
/* Server error counts. */
#define CENSUS_METRIC_RPC_SERVER_ERRORS ((uint32_t)3)
/* Client side request latency. */
#define CENSUS_METRIC_RPC_CLIENT_LATENCY ((uint32_t)4)
/* Server side request latency. */
#define CENSUS_METRIC_RPC_SERVER_LATENCY ((uint32_t)5)

#endif /* GRPC_CORE_EXT_CENSUS_RPC_METRIC_ID_H */
