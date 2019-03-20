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
template <class T1, class T2>
struct Pair {
 public:
  Pair(T1&& u, T2&& v) : first(std::move(u)), second(std::move(v)) {}
  Pair(const T1& u, const T2& v) : first(u), second(v) {}
  void swap(Pair& other) {
    T1 temp_first = std::move(first);
    T2 temp_second = std::move(second);
    first = std::move(other.first);
    second = std::move(other.second);
    other.first = std::move(temp_first);
    other.second = std::move(temp_second);
  }
  T1 first;
  T2 second;
};

template <class T1, class T2>
Pair<T1, T2> MakePair(T1&& u, T2&& v) {
  return Pair<T1, T2>(std::move(u), std::move(v));
}

template <class T1, class T2>
Pair<T1, T2> MakePair(const T1& u, const T2& v) {
  return Pair<T1, T2>(u, v);
}
}  // namespace grpc_core
#endif /* GRPC_CORE_LIB_GPRPP_PAIR_H */
