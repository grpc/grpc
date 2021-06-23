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

#ifndef GRPC_CORE_LIB_PROMISE_DETAIL_JOIN_H
#define GRPC_CORE_LIB_PROMISE_DETAIL_JOIN_H

#include <bitset>
#include <utility>
#include "absl/utility/utility.h"
#include "src/core/lib/promise/adaptor.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {
namespace promise_detail {

template <typename Traits, typename F>
union Fused {
  explicit Fused(F f) : f(std::move(f)) {}
  ~Fused() {}
  [[no_unique_address]] F f;
  using Result =
      absl::remove_reference_t<typename Traits::template ResultType<decltype(
          std::move(*f().get_ready()))>>;
  [[no_unique_address]] Result result;
};

template <typename Traits, size_t I, typename... Fs>
struct Joint : public Joint<Traits, I + 1, Fs...> {
  using F = typename std::tuple_element<I, std::tuple<Fs...>>::type;
  using Fsd = Fused<Traits, F>;
  using Bits = std::bitset<sizeof...(Fs)>;
  [[no_unique_address]] Fsd fused;
  explicit Joint(std::tuple<Fs*...> fs)
      : Joint<Traits, I + 1, Fs...>(fs), fused(std::move(*std::get<I>(fs))) {}
  Joint(const Joint& j) : Joint<Traits, I + 1, Fs...>(j), fused(j.fused.f) {}
  Joint(Joint&& j) noexcept
      : Joint<Traits, I + 1, Fs...>(
            std::forward<Joint<Traits, I + 1, Fs...>>(j)),
        fused(std::move(j.fused.f)) {}
  void DestructAll(const Bits& bits) {
    if (!static_cast<bool>(bits[I])) {
      Destruct(&fused.f);
    } else {
      Destruct(&fused.result);
    }
    Joint<Traits, I + 1, Fs...>::DestructAll(bits);
  }
  template <typename F>
  auto Run(Bits* bits, F finally) -> decltype(finally()) {
    if (!static_cast<bool>((*bits)[I])) {
      auto r = fused.f();
      if (auto* p = r.get_ready()) {
        return Traits::OnResult(
            std::move(*p), [this, bits, &finally](typename Fsd::Result result) {
              bits->set(I);
              Destruct(&fused.f);
              Construct(&fused.result, std::move(result));
              return Joint<Traits, I + 1, Fs...>::Run(bits, std::move(finally));
            });
      }
    }
    return Joint<Traits, I + 1, Fs...>::Run(bits, std::move(finally));
  }
};

template <typename Traits, typename... Fs>
struct Joint<Traits, sizeof...(Fs), Fs...> {
  explicit Joint(std::tuple<Fs*...>) {}
  Joint(const Joint&) {}
  Joint(Joint&&) noexcept {}
  template <typename T>
  void DestructAll(const T&) {}
  template <typename F>
  auto Run(std::bitset<sizeof...(Fs)>*, F finally) -> decltype(finally()) {
    return finally();
  }
};

template <typename Traits, typename... Fs>
class Join {
 private:
  static constexpr size_t N = sizeof...(Fs);
  [[no_unique_address]] std::bitset<N> state_;
  union {
    [[no_unique_address]] Joint<Traits, 0, Fs...> joints_;
  };

  template <size_t I>
  Joint<Traits, I, Fs...>* GetJoint() {
    return static_cast<Joint<Traits, I, Fs...>*>(&joints_);
  }

  using Tuple = std::tuple<typename Traits::template ResultType<decltype(
      std::move(*std::declval<Fs>()().get_ready()))>...>;

  template <size_t... I>
  Tuple Finish(absl::index_sequence<I...>) {
    return Tuple(std::move(GetJoint<I>()->fused.result)...);
  }

 public:
  explicit Join(Fs&&... fs) : joints_(std::tuple<Fs*...>(&fs...)) {}
  Join& operator=(const Join&) = delete;
  Join(const Join& other) {
    assert(other.state_.none());
    Construct(&joints_, other.joints_);
  }
  Join(Join&& other) noexcept {
    assert(other.state_.none());
    Construct(&joints_, std::move(other.joints_));
  }
  ~Join() { joints_.DestructAll(state_); }
  using Result = decltype(Traits::Wrap(std::declval<Tuple>()));
  Poll<Result> operator()() {
    return joints_.Run(&state_, [this]() -> Poll<Result> {
      if (state_.all()) {
        return ready(Traits::Wrap(Finish(absl::make_index_sequence<N>())));
      } else {
        return Pending();
      }
    });
  }
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif
