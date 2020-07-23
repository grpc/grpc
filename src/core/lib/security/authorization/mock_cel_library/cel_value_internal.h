/*
 *
 * Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_CEL_INTERNAL_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_CEL_INTERNAL_H

#include <grpc/support/port_platform.h>

#include "absl/types/variant.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace internal {

// Helper classes needed for IndexOf metafunction implementation.
template <int N, bool>
struct IndexDef {};

// This partial IndexDef type specialization provides additional constant
// "value", associated with the type.
template <int N>
struct IndexDef<N, true> {
  static constexpr int value = N;
};

// TypeIndexer is a template class, representing metafunction to find the index
// of a type in a type list.
template <int N, int TYPE_SIZE, class Type, class TypeToTest, class... Types>
struct TypeIndexer
    : public TypeIndexer<N + 1, sizeof...(Types), Type, Types...>,
      IndexDef<N, std::is_same<Type, TypeToTest>::value> {};

template <int N, class Type, class TypeToTest>
struct TypeIndexer<N, 1, Type, TypeToTest>
    : public IndexDef<N, std::is_same<Type, TypeToTest>::value> {};

// ValueHolder class wraps absl::variant, adding IndexOf metafunction to it.
template <class... Args>
class ValueHolder {
 public:
  template <class T>
  explicit ValueHolder(T t) : value_(t) {}

  // Metafunction to find the index of a type in a type list.
  template <class T>
  using IndexOf = TypeIndexer<0, sizeof...(Args), T, Args...>;

  template <class T>
  const T* get() const {
    return absl::get_if<T>(&value_);
  }

  template <class T>
  bool is() const {
    return absl::holds_alternative<T>(value_);
  }

  int index() const { return value_.index(); }

  template <class ReturnType, class Op>
  ReturnType Visit(Op&& op) const {
    return absl::visit(std::forward<Op>(op), value_);
  }

 private:
  absl::variant<Args...> value_;
};

}  // namespace internal

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MOCK_CEL_LIBRARY_CEL_INTERNAL_H