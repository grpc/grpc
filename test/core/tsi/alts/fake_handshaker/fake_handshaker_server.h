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

#ifndef TEST_CORE_TSI_ALTS_FAKE_HANDSHAKER_FAKE_HANDSHAKER_SERVER_H
#define TEST_CORE_TSI_ALTS_FAKE_HANDSHAKER_FAKE_HANDSHAKER_SERVER_H

#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

namespace grpc {
namespace gcp {

// If max_expected_concurrent_rpcs is non-zero, the fake handshake service
// will track the number of concurrent RPCs that it handles and abort
// if if ever exceeds that number.
std::unique_ptr<grpc::Service> CreateFakeHandshakerService(
    int expected_max_concurrent_rpcs);

}  // namespace gcp
}  // namespace grpc

#endif  // TEST_CORE_TSI_ALTS_FAKE_HANDSHAKER_FAKE_HANDSHAKER_SERVER_H
