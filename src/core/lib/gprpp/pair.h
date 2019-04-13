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

#include <utility>

namespace grpc_core {
template <class T1, class T2>
using Pair = std::pair<T1, T2>;

template <class T1, class T2>
inline Pair<typename std::decay<T1>::type, typename std::decay<T2>::type>
MakePair(T1&& u, T2&& v) {
  typedef typename std::decay<T1>::type V1;
  typedef typename std::decay<T2>::type V2;
  return Pair<V1, V2>(std::forward<T1>(u), std::forward<T2>(v));
}
}  // namespace grpc_core
#endif /* GRPC_CORE_LIB_GPRPP_PAIR_H */
