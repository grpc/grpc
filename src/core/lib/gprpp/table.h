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

#ifndef GRPC_CORE_LIB_GPRPP_TYPED_TABLE_H
#define GRPC_CORE_LIB_GPRPP_TYPED_TABLE_H

// Portable code. port_platform.h is not required.

#include <bitset>
#include <utility>

namespace grpc_core {
namespace typed_table_detail {

template <typename T>
struct Element {
  union U {
    U() {}
    ~U() {}
    [[no_unique_address]] T x;
  };
  U u;
};

template <typename Ignored, typename Needle, typename... Haystack>
struct IndexOfStruct;
template <typename Needle, typename Straw, typename... RestOfHaystack>
struct IndexOfStruct<
    typename std::enable_if<std::is_same<Needle, Straw>::value>::type, Needle,
    Straw, RestOfHaystack...> {
  static const size_t N = 0;
};
template <typename Needle, typename Straw, typename... RestOfHaystack>
struct IndexOfStruct<
    typename std::enable_if<!std::is_same<Needle, Straw>::value>::type, Needle,
    Straw, RestOfHaystack...> {
  static const size_t N = 1 + IndexOfStruct<void, Needle, RestOfHaystack...>::N;
};

template <typename Needle, typename... Haystack>
constexpr size_t IndexOf() {
  return IndexOfStruct<void, Needle, Haystack...>::N;
}

template <typename T>
void do_these_things(std::initializer_list<T>) {}

}  // namespace typed_table_detail

template <typename... Ts>
class TypedTable : private typed_table_detail::Element<Ts>... {
  template <typename T>
  using El = typed_table_detail::Element<T>;

 public:
  TypedTable() = default;
  ~TypedTable() {
    typed_table_detail::do_these_things({
        (clear<Ts>(), 1)...,
    });
  }

  TypedTable(const TypedTable& rhs) {
    typed_table_detail::do_these_things(
        {(copy_if<false>(rhs.get<Ts>()), 1)...});
  }

  TypedTable& operator=(const TypedTable& rhs) {
    typed_table_detail::do_these_things({(copy_if<true>(rhs.get<Ts>()), 1)...});
    return *this;
  }

  TypedTable(TypedTable&& rhs);
  TypedTable& operator=(TypedTable&& rhs);

  template <typename T>
  bool has() const {
    return present_bits_[index_of<T>()];
  }

  template <typename T>
  T* get() {
    if (has<T>()) return element_ptr<T>();
    return nullptr;
  }

  template <typename T>
  const T* get() const {
    if (has<T>()) return element_ptr<T>();
    return nullptr;
  }

  template <typename T>
  T* get_or_create() {
    T* p = element_ptr<T>();
    if (!set_present<T>(true)) {
      new (p) T();
    }
    return element_ptr<T>();
  }

  template <typename T, typename... Args>
  T* set(Args&&... args) {
    T* p = element_ptr<T>();
    if (set_present<T>(true)) {
      *p = T(std::forward<Args>(args)...);
    } else {
      new (p) T(std::forward<Args>(args)...);
    }
    return p;
  }

  template <typename T>
  void clear() {
    if (set_present<T>(false)) {
      element_ptr<T>()->~T();
    }
  }

 private:
  using PresentBits = std::bitset<sizeof...(Ts)>;

  template <typename T>
  static constexpr size_t index_of() {
    return typed_table_detail::IndexOf<T, Ts...>();
  }

  template <typename T>
  typename PresentBits::reference present_bit() {
    return present_bits_[index_of<T>()];
  }

  template <typename T>
  T* element_ptr() {
    return &static_cast<El<T>*>(this)->u.x;
  }

  template <typename T>
  const T* element_ptr() const {
    return &static_cast<const El<T>*>(this)->u.x;
  }

  template <typename T>
  bool set_present(bool value) {
    typename PresentBits::reference b = present_bit<T>();
    bool out = b;
    b = value;
    return out;
  }

  template <bool or_clear, typename T>
  void copy_if(const T* p) {
    if (p) {
      set<T>(*p);
    } else if (or_clear) {
      clear<T>();
    }
  }

  template <bool or_clear, typename T>
  void move_if(T* p) {
    if (p) {
      set<T>(std::move(*p));
    } else if (or_clear) {
      clear<T>();
    }
  }

  PresentBits present_bits_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_TYPED_TABLE_H
