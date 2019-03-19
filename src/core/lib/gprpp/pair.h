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

#ifndef GRPC_CORE_LIB_GPRPP_PAIR_H
#define GRPC_CORE_LIB_GPRPP_PAIR_H

#include <grpc/support/port_platform.h>

namespace grpc_core {
// Alternative to std::pair for grpc_core
template <class Key, class T>
struct pair {
 public:
  pair(Key k, T v) : first(std::move(k)), second(std::move(v)) {}
  void swap(pair& other) {
    Key temp_first = std::move(first);
    T temp_second = std::move(second);
    first = std::move(other.first);
    second = std::move(other.second);
    other.first = std::move(temp_first);
    other.second = std::move(temp_second);
  }
  Key first;
  T second;

 private:
};

template <class Key, class T>
pair<Key, T> make_pair(Key&& k, T&& v) {
  return std::move(pair<Key, T>(std::move(k), std::move(v)));
}
}  // namespace grpc_core
#endif /* GRPC_CORE_LIB_GPRPP_PAIR_H */
