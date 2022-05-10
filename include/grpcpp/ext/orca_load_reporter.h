/*
 *
 * Copyright 2022 gRPC authors.
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

#ifndef GRPCPP_EXT_ORCA_LOAD_REPORTER_H
#define GRPCPP_EXT_ORCA_LOAD_REPORTER_H

namespace grpc {
class ServerBuilder;

// Registers the per-rpc orca load reporter into the \a ServerBuilder.
// Once this is done, the server will automatically send the load metrics
// after each RPC as they were reported. In order to report load metrics,
// call the \a ServerContext::GetCallMetricRecorder() method to retrieve
// the recorder for the current call.
void RegisterCallMetricLoadReporter(ServerBuilder*);

}  // namespace grpc

#endif  // GRPCPP_EXT_ORCA_LOAD_REPORTER_H
