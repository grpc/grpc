// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_POPULARITY_COUNT_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_POPULARITY_COUNT_H

#include <grpc/impl/codegen/port_platform.h>

namespace grpc_core {

// filter tables for elems: this tables provides an approximate
// popularity count for particular hashes, and are used to determine whether
// a new literal should be added to the compression table or not.
// They track a single integer that counts how often a particular value has
// been seen. When that count reaches max (255), all values are halved. */
template <uint8_t kElems>
class PopularityCount {
 public:
  PopularityCount() : sum_{0}, elems_{} {}

  // increment a filter count, halve all counts if one element reaches max
  // return true if this element seems to be popular, false otherwise
  bool AddElement(uint8_t idx) {
    elems_[idx]++;
    if (GPR_LIKELY(elems_[idx] < 255)) {
      sum_++;
    } else {
      HalveFilter();
    }
    return elems_[idx] >= 2 * sum_ / kElems;
  }

 private:
  // halve all counts because an element reached max
  void HalveFilter() {
    sum_ = 0;
    for (int i = 0; i < kElems; i++) {
      elems_[i] /= 2;
      sum_ += elems_[i];
    }
  }

  uint32_t sum_;
  uint8_t elems_[kElems];
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_POPULARITY_COUNT_H */
