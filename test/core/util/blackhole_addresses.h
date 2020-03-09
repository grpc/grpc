/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef TEST_CORE_UTIL_BLACKHOLE_ADDRESSES_H
#define TEST_CORE_UTIL_BLACKHOLE_ADDRESSES_H

#include <grpc/support/port_platform.h>

#include <string>

#include <grpc/support/log.h>

namespace grpc_core {
namespace testing {

const std::string GetBlackHoledIPv6Address();

}  // namespace testing
}  // namespace grpc_core

#endif  // TEST_CORE_UTIL_BLACKHOLE_ADDRESSES_H
