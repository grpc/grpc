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

#ifndef GRPC_TEST_CORE_UTIL_GPRPP_MAP_TESTER_H
#define GRPC_TEST_CORE_UTIL_GPRPP_MAP_TESTER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/map.h"

namespace grpc_core {
namespace testing {
template <class Key, class T, class Compare>
class MapTester {
  using GrpcMap = typename ::grpc_core::Map<Key, T, Compare>;
  using GrpcMapEntry = typename ::grpc_core::Map<Key, T, Compare>::Entry;

 public:
  MapTester(GrpcMap* test_map) : map_(test_map) {}
  GrpcMapEntry* Root() { return map_->root_; }

  GrpcMapEntry* Left(typename Map<Key, T, Compare>::Entry* e) {
    return e->left;
  }

  GrpcMapEntry* Right(typename Map<Key, T, Compare>::Entry* e) {
    return e->right;
  }

 private:
  Map<Key, T, Compare>* map_;
};
}  // namespace testing
}  // namespace grpc_core
#endif /* GRPC_TEST_CORE_UTIL_GPRPP_MAP_TESTER_H */
