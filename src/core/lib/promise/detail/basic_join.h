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

#ifndef GRPC_SRC_CORE_LIB_PROMISE_DETAIL_BASIC_JOIN_H
#define GRPC_SRC_CORE_LIB_PROMISE_DETAIL_BASIC_JOIN_H

#include <grpc/support/port_platform.h>

#include <assert.h>
#include <stddef.h>

#include <array>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/utility/utility.h"

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {
namespace promise_detail {

// This union can either be a functor, or the result of the functor (after
// mapping via a trait). Allows us to remember the result of one joined functor
// until the rest are ready.
template <typename Traits, typename F>
union Fused {
  explicit Fused(F&& f) : f(std::forward<F>(f)) {}
  explicit Fused(PromiseLike<F>&& f) : f(std::forward<PromiseLike<F>>(f)) {}
  ~Fused() {}
  // Wrap the functor in a PromiseLike to handle immediately returning functors
  // and the like.
  using Promise = PromiseLike<F>;
  GPR_NO_UNIQUE_ADDRESS Promise f;
  // Compute the result type: We take the result of the promise, and pass it via
  // our traits, so that, for example, TryJoin and take a StatusOr<T> and just
  // store a T.
  using Result = typename Traits::template ResultType<typename Promise::Result>;
  GPR_NO_UNIQUE_ADDRESS Result result;
};

// A join gets composed of joints... these are just wrappers around a Fused for
// their data, with some machinery as methods to get the system working.
template <typename Traits, size_t kRemaining, typename... Fs>
struct Joint : public Joint<Traits, kRemaining - 1, Fs...> {
  // The index into Fs for this Joint
  static constexpr size_t kIdx = sizeof...(Fs) - kRemaining;
  // The next join (the one we derive from)
  using NextJoint = Joint<Traits, kRemaining - 1, Fs...>;
  // From Fs, extract the functor for this joint.
  using F = typename std::tuple_element<kIdx, std::tuple<Fs...>>::type;
  // Generate the Fused type for this functor.
  using Fsd = Fused<Traits, F>;
  GPR_NO_UNIQUE_ADDRESS Fsd fused;
  // Figure out what kind of bitmask will be used by the outer join.
  using Bits = BitSet<sizeof...(Fs)>;
  // Initialize from a tuple of pointers to Fs
  explicit Joint(std::tuple<Fs*...> fs)
      : NextJoint(fs), fused(std::move(*std::get<kIdx>(fs))) {}
  // Copy: assume that the Fuse is still in the promise state (since it's not
  // legal to copy after the first poll!)
  Joint(const Joint& j) : NextJoint(j), fused(j.fused.f) {}
  // Move: assume that the Fuse is still in the promise state (since it's not
  // legal to move after the first poll!)
  Joint(Joint&& j) noexcept
      : NextJoint(std::forward<NextJoint>(j)), fused(std::move(j.fused.f)) {}
  // Destruct: check bits to see if we're in promise or result state, and call
  // the appropriate destructor. Recursively, call up through the join.
  void DestructAll(const Bits& bits) {
    if (!bits.is_set(kIdx)) {
      Destruct(&fused.f);
    } else {
      Destruct(&fused.result);
    }
    NextJoint::DestructAll(bits);
  }
  // Poll all joints up, and then call finally.
  template <typename F>
  auto Run(Bits* bits, F finally) -> decltype(finally()) {
    // If we're still in the promise state...
    if (!bits->is_set(kIdx)) {
      // Poll the promise
      auto r = fused.f();
      if (auto* p = r.value_if_ready()) {
        // If it's done, then ask the trait to unwrap it and store that result
        // in the Fused, and continue the iteration. Note that OnResult could
        // instead choose to return a value instead of recursing through the
        // iteration, in that case we continue returning the same result up.
        // Here is where TryJoin can escape out.
        return Traits::OnResult(
            std::move(*p), [this, bits, &finally](typename Fsd::Result result) {
              bits->set(kIdx);
              Destruct(&fused.f);
              Construct(&fused.result, std::move(result));
              return NextJoint::Run(bits, std::move(finally));
            });
      }
    }
    // That joint is still pending... we'll still poll the result of the joints.
    return NextJoint::Run(bits, std::move(finally));
  }
};

// Terminating joint... for each of the recursions, do the thing we're supposed
// to do at the end.
template <typename Traits, typename... Fs>
struct Joint<Traits, 0, Fs...> {
  explicit Joint(std::tuple<Fs*...>) {}
  Joint(const Joint&) {}
  Joint(Joint&&) noexcept {}
  template <typename T>
  void DestructAll(const T&) {}
  template <typename F>
  auto Run(BitSet<sizeof...(Fs)>*, F finally) -> decltype(finally()) {
    return finally();
  }
};

template <typename Traits, typename... Fs>
class BasicJoin {
 private:
  // How many things are we joining?
  static constexpr size_t N = sizeof...(Fs);
  // Bitset: if a bit is 0, that joint is still in promise state. If it's 1,
  // then the joint has a result.
  GPR_NO_UNIQUE_ADDRESS BitSet<N> state_;
  // The actual joints, wrapped in an anonymous union to give us control of
  // construction/destruction.
  union {
    GPR_NO_UNIQUE_ADDRESS Joint<Traits, sizeof...(Fs), Fs...> joints_;
  };

  // Access joint index I
  template <size_t I>
  Joint<Traits, sizeof...(Fs) - I, Fs...>* GetJoint() {
    return static_cast<Joint<Traits, sizeof...(Fs) - I, Fs...>*>(&joints_);
  }

  // The tuple of results of all our promises
  using Tuple = std::tuple<typename Fused<Traits, Fs>::Result...>;

  // Collect up all the results and construct a tuple.
  template <size_t... I>
  Tuple Finish(absl::index_sequence<I...>) {
    return Tuple(std::move(GetJoint<I>()->fused.result)...);
  }

 public:
  explicit BasicJoin(Fs&&... fs) : joints_(std::tuple<Fs*...>(&fs...)) {}
  BasicJoin& operator=(const BasicJoin&) = delete;
  // Copy a join - only available before polling.
  BasicJoin(const BasicJoin& other) {
    assert(other.state_.none());
    Construct(&joints_, other.joints_);
  }
  // Move a join - only available before polling.
  BasicJoin(BasicJoin&& other) noexcept {
    assert(other.state_.none());
    Construct(&joints_, std::move(other.joints_));
  }
  ~BasicJoin() { joints_.DestructAll(state_); }
  using Result = decltype(Traits::Wrap(std::declval<Tuple>()));
  // Poll the join
  Poll<Result> operator()() {
    // Poll the joints...
    return joints_.Run(&state_, [this]() -> Poll<Result> {
      // If all of them are completed, collect the results, and then ask our
      // traits to wrap them - allowing for example TryJoin to turn tuple<A,B,C>
      // into StatusOr<tuple<A,B,C>>.
      if (state_.all()) {
        return Traits::Wrap(Finish(absl::make_index_sequence<N>()));
      } else {
        return Pending();
      }
    });
  }
};

}  // namespace promise_detail
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_PROMISE_DETAIL_BASIC_JOIN_H
