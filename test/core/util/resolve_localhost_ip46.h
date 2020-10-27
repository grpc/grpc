//
//
// Copyright 2020 gRPC authors.
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
//

#ifndef GRPC_TEST_CORE_UTIL_RESOLVE_LOCALHOST_IP46_H_
#define GRPC_TEST_CORE_UTIL_RESOLVE_LOCALHOST_IP46_H_

namespace grpc_core {

// Test whether localhost resolves to ipv4 and/or ipv6
void LocalhostResolves(bool* ipv4, bool* ipv6);

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_RESOLVE_LOCALHOST_IP46_H_
