/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SUPPORT_MPSCQ_H
#define GRPC_CORE_LIB_SUPPORT_MPSCQ_H

#include <utility>

// Work-alike for std::tuple
// Also implements TupleCall(F, Tuple<...>), which calls F with arguments
// specified by a tuple

namespace grpc_core {
namespace tuple_impl {

template <int...>
class IntSeq {};

template <typename Sequence, int N>
struct CatIntSeq;
template <int... Ints, int N>
struct CatIntSeq<IntSeq<Ints...>, N> {
  typedef IntSeq<Ints..., N> Type;
};

template <int N>
struct GenSeq {
  typedef typename CatIntSeq<typename GenSeq<N - 1>::Type, N - 1>::Type Type;
};
template <>
struct GenSeq<0> {
  typedef IntSeq<> Type;
};

template <int N>
using MakeIntSeq = typename GenSeq<N>::Type;

template <int N, typename... Types>
struct TypeAtIndex;

template <typename Head, typename... Tail>
struct TypeAtIndex<0, Head, Tail...> {
  typedef Head Type;
};

template <int N, typename Head, typename... Tail>
struct TypeAtIndex<N, Head, Tail...> {
  typedef typename TypeAtIndex<N - 1, Tail...>::Type Type;
};

template <int index, typename T>
class Element {
 public:
  Element() = default;
  template <class U>
  Element(const U& val) : field(val) {}
  template <class U>
  Element(U&& val) : field(std::move(val)) {}
  T field;
};

template <typename Sequence, typename... Fields>
class TupleImpl;

template <int... Indices, typename... Types>
class TupleImpl<IntSeq<Indices...>, Types...>
    : public Element<Indices, Types>... {
 public:
  TupleImpl() = default;
  TupleImpl(const TupleImpl&) = default;
  TupleImpl(TupleImpl&&) = default;

  template <typename... OtherTypes>
  TupleImpl(OtherTypes&&... values) : Element<Indices, Types>(values)... {}
};

template <class T>
T Example();

}  // namespace tuple_impl

template <typename... Types>
class Tuple
    : public tuple_impl::TupleImpl<tuple_impl::MakeIntSeq<sizeof...(Types)>,
                                   Types...> {
  typedef tuple_impl::TupleImpl<tuple_impl::MakeIntSeq<sizeof...(Types)>,
                                Types...>
      Base;

 public:
  Tuple() = default;
  Tuple(const Tuple&) = default;
  Tuple(Tuple&&) = default;

  template <typename... OtherTypes,
            typename = typename std::enable_if<sizeof...(OtherTypes) ==
                                               sizeof...(Types)>::type>
  explicit Tuple(OtherTypes&&... elements)
      : Base(std::forward<OtherTypes>(elements)...) {}

  template <typename... OtherTypes>
  explicit Tuple(Tuple<OtherTypes...> const& rhs) : Base(rhs) {}

  template <typename... OtherTypes>
  explicit Tuple(Tuple<OtherTypes...>&& rhs) : Base(std::move(rhs)) {}
};

template <int N, typename... Types>
typename tuple_impl::TypeAtIndex<N, Types...>::Type& get(
    Tuple<Types...>& tuple) {
  typedef typename tuple_impl::TypeAtIndex<N, Types...>::Type FieldType;
  typedef typename tuple_impl::Element<N, FieldType> ElemType;
  return static_cast<ElemType&>(tuple).field;
}

template <int N, typename... Types>
const typename tuple_impl::TypeAtIndex<N, Types...>::Type& get(
    const Tuple<Types...>& tuple) {
  typedef typename tuple_impl::TypeAtIndex<N, Types...>::Type FieldType;
  typedef typename tuple_impl::Element<N, FieldType> ElemType;
  return static_cast<const ElemType&>(tuple).field;
}

namespace tuple_impl {

template <typename F, typename... Types, int... ints>
auto TupleCallImpl(F&& f, Tuple<Types...>&& args, IntSeq<ints...>)
    -> decltype(f(tuple_impl::Example<Types>()...)) {
  return f(get<ints>(std::forward<Tuple<Types...>>(args))...);
}
}

template <typename F, typename... Types>
auto TupleCall(F&& f, Tuple<Types...>& args)
    -> decltype(f(tuple_impl::Example<Types>()...)) {
  return tuple_impl::TupleCallImpl(std::forward<F>(f),
                                   std::forward<Tuple<Types...>>(args),
                                   tuple_impl::MakeIntSeq<sizeof...(Types)>());
}

template <typename F, typename... Types>
auto TupleCall(F&& f, const Tuple<Types...>& args)
    -> decltype(f(tuple_impl::Example<Types>()...)) {
  return tuple_impl::TupleCallImpl(std::forward<F>(f),
                                   std::forward<Tuple<Types...>>(args),
                                   tuple_impl::MakeIntSeq<sizeof...(Types)>());
}

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SUPPORT_MPSCQ_H */
