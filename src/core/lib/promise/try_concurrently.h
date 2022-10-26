// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_PROMISE_TRY_CONCURRENTLY_H
#define GRPC_CORE_LIB_PROMISE_TRY_CONCURRENTLY_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <cstdint>
#include <utility>

#include "absl/types/variant.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/poll.h"

namespace grpc_core {

namespace promise_detail {

template <typename Promise>
struct Necessary {
  PromiseLike<Promise> promise;
  static constexpr bool must_complete() { return true; }
};

template <typename Promise>
struct Helper {
  PromiseLike<Promise> promise;
  static constexpr bool must_complete() { return false; }
};

// A set of promises that can be polled concurrently.
// Fuses them when completed (that is, destroys the promise and records it
// completed).
// Relies on an external bit field to handle the recording: this saves a bunch
// of space, but means the implementation of this type is weird: it's really
// super tied to TryConcurrently and no attempt should be made to use this
// independently.
template <typename... Ts>
class FusedSet;

template <typename T, typename... Ts>
class FusedSet<T, Ts...> : public FusedSet<Ts...> {
 public:
  explicit FusedSet(T&& x, Ts&&... xs)
      : FusedSet<Ts...>(std::forward<T>(xs)...) {
    Construct(&wrapper_, std::forward<T>(x));
  }
  explicit FusedSet(T&& x, FusedSet<Ts...>&& xs)
      : FusedSet<Ts...>(std::forward<FusedSet<Ts...>>(xs)) {
    Construct(&wrapper_, std::forward<T>(x));
  }
  // Empty destructor: consumers must call Destroy() to ensure cleanup occurs
  ~FusedSet() {}

  FusedSet(const FusedSet&) = delete;
  FusedSet& operator=(const FusedSet&) = delete;

  // Assumes all 'done_bits' for other are 0 and will be set to 1
  FusedSet(FusedSet&& other) noexcept : FusedSet<Ts...>(std::move(other)) {
    Construct(&wrapper_, std::move(other.wrapper_));
    Destruct(&other.wrapper_);
  }

  static constexpr size_t Size() { return 1 + sizeof...(Ts); }

  static constexpr uint8_t NecessaryBits() {
    return (T::must_complete() ? 1 : 0) |
           (FusedSet<Ts...>::NecessaryBits() << 1);
  }

  template <int kDoneBit>
  void Destroy(uint8_t done_bits) {
    if ((done_bits & (1 << kDoneBit)) == 0) {
      Destruct(&wrapper_);
    }
    FusedSet<Ts...>::template Destroy<kDoneBit + 1>(done_bits);
  }

  template <typename Result, int kDoneBit>
  Poll<Result> Run(uint8_t& done_bits) {
    if ((done_bits & (1 << kDoneBit)) == 0) {
      auto p = wrapper_.promise();
      if (auto* status = absl::get_if<kPollReadyIdx>(&p)) {
        done_bits |= (1 << kDoneBit);
        Destruct(&wrapper_);
        if (!IsStatusOk(*status)) {
          return StatusCast<Result>(std::move(*status));
        }
      }
    }
    return FusedSet<Ts...>::template Run<Result, kDoneBit + 1>(done_bits);
  }

  template <typename P>
  FusedSet<P, T, Ts...> With(P x) {
    return FusedSet<P, T, Ts...>(std::move(x), std::move(*this));
  }

 private:
  union {
    T wrapper_;
  };
};

template <>
class FusedSet<> {
 public:
  static constexpr size_t Size() { return 0; }
  static constexpr uint8_t NecessaryBits() { return 0; }

  template <typename Result, int kDoneBit>
  Poll<Result> Run(uint8_t) {
    return Pending{};
  }
  template <int kDoneBit>
  void Destroy(uint8_t) {}

  template <typename P>
  FusedSet<P> With(P x) {
    return FusedSet<P>(std::move(x));
  }
};

template <typename Main, typename PreMain, typename PostMain>
class TryConcurrently {
 public:
  TryConcurrently(Main main, PreMain pre_main, PostMain post_main)
      : done_bits_(0),
        pre_main_(std::move(pre_main)),
        post_main_(std::move(post_main)) {
    Construct(&main_, std::move(main));
  }

  TryConcurrently(const TryConcurrently&) = delete;
  TryConcurrently& operator=(const TryConcurrently&) = delete;
  TryConcurrently(TryConcurrently&& other) noexcept
      : done_bits_(0),
        pre_main_(std::move(other.pre_main_)),
        post_main_(std::move(other.post_main_)) {
    GPR_DEBUG_ASSERT(other.done_bits_ == 0);
    other.done_bits_ = HelperBits();
    Construct(&main_, std::move(other.main_));
  }
  TryConcurrently& operator=(TryConcurrently&& other) noexcept {
    GPR_DEBUG_ASSERT(other.done_bits_ == 0);
    done_bits_ = 0;
    other.done_bits_ = HelperBits();
    pre_main_ = std::move(other.pre_main_);
    post_main_ = std::move(other.post_main_);
    Construct(&main_, std::move(other.main_));
    return *this;
  }

  ~TryConcurrently() {
    if (done_bits_ & 1) {
      Destruct(&result_);
    } else {
      Destruct(&main_);
    }
    pre_main_.template Destroy<1>(done_bits_);
    post_main_.template Destroy<1 + PreMain::Size()>(done_bits_);
  }

  using Result =
      typename PollTraits<decltype(std::declval<PromiseLike<Main>>()())>::Type;

  Poll<Result> operator()() {
    auto r = pre_main_.template Run<Result, 1>(done_bits_);
    if (auto* status = absl::get_if<Result>(&r)) {
      GPR_DEBUG_ASSERT(!IsStatusOk(*status));
      return std::move(*status);
    }
    if ((done_bits_ & 1) == 0) {
      auto p = main_();
      if (auto* status = absl::get_if<kPollReadyIdx>(&p)) {
        done_bits_ |= 1;
        Destruct(&main_);
        Construct(&result_, std::move(*status));
      }
    }
    r = post_main_.template Run<Result, 1 + PreMain::Size()>(done_bits_);
    if (auto* status = absl::get_if<Result>(&r)) {
      GPR_DEBUG_ASSERT(!IsStatusOk(*status));
      return std::move(*status);
    }
    if ((done_bits_ & NecessaryBits()) == NecessaryBits()) {
      return std::move(result_);
    }
    return Pending{};
  }

  template <typename P>
  auto NecessaryPush(P p);
  template <typename P>
  auto NecessaryPull(P p);
  template <typename P>
  auto Push(P p);
  template <typename P>
  auto Pull(P p);

 private:
  // Bitmask for done_bits_ specifying which promises must be completed prior to
  // returning ok.
  constexpr uint8_t NecessaryBits() {
    return 1 | (PreMain::NecessaryBits() << 1) |
           (PostMain::NecessaryBits() << (1 + PreMain::Size()));
  }
  // Bitmask for done_bits_ specifying what all of the promises being complete
  // would look like.
  constexpr uint8_t AllBits() {
    return (1 << (1 + PreMain::Size() + PostMain::Size())) - 1;
  }
  // Bitmask of done_bits_ specifying which bits correspond to helper promises -
  // that is all promises that are not the main one.
  constexpr uint8_t HelperBits() { return AllBits() ^ 1; }

  // done_bits signifies which operations have completed.
  // Bit 0 is set if main_ has completed.
  // The next higher bits correspond one per pre-main promise.
  // The next higher bits correspond one per post-main promise.
  // So, going from most significant bit to least significant:
  // +--------------+-------------+--------+
  // |post_main bits|pre_main bits|main bit|
  // +--------------+-------------+--------+
  uint8_t done_bits_;
  PreMain pre_main_;
  union {
    PromiseLike<Main> main_;
    Result result_;
  };
  PostMain post_main_;
};

template <typename Main, typename PreMain, typename PostMain>
auto MakeTryConcurrently(Main&& main, PreMain&& pre_main,
                         PostMain&& post_main) {
  return TryConcurrently<Main, PreMain, PostMain>(
      std::forward<Main>(main), std::forward<PreMain>(pre_main),
      std::forward<PostMain>(post_main));
}

template <typename Main, typename PreMain, typename PostMain>
template <typename P>
auto TryConcurrently<Main, PreMain, PostMain>::NecessaryPush(P p) {
  GPR_DEBUG_ASSERT(done_bits_ == 0);
  done_bits_ = HelperBits();
  return MakeTryConcurrently(std::move(main_),
                             pre_main_.With(Necessary<P>{std::move(p)}),
                             std::move(post_main_));
}

template <typename Main, typename PreMain, typename PostMain>
template <typename P>
auto TryConcurrently<Main, PreMain, PostMain>::NecessaryPull(P p) {
  GPR_DEBUG_ASSERT(done_bits_ == 0);
  done_bits_ = HelperBits();
  return MakeTryConcurrently(std::move(main_), std::move(pre_main_),
                             post_main_.With(Necessary<P>{std::move(p)}));
}

template <typename Main, typename PreMain, typename PostMain>
template <typename P>
auto TryConcurrently<Main, PreMain, PostMain>::Push(P p) {
  GPR_DEBUG_ASSERT(done_bits_ == 0);
  done_bits_ = HelperBits();
  return MakeTryConcurrently(std::move(main_),
                             pre_main_.With(Helper<P>{std::move(p)}),
                             std::move(post_main_));
}

template <typename Main, typename PreMain, typename PostMain>
template <typename P>
auto TryConcurrently<Main, PreMain, PostMain>::Pull(P p) {
  GPR_DEBUG_ASSERT(done_bits_ == 0);
  done_bits_ = HelperBits();
  return MakeTryConcurrently(std::move(main_), std::move(pre_main_),
                             post_main_.With(Helper<P>{std::move(p)}));
}

}  // namespace promise_detail

// TryConcurrently runs a set of promises concurrently.
// There is a structure to the promises:
//  - A 'main' promise dominates the others - it must complete before the
//    overall promise successfully completes. Its result is chosen in the event
//    of successful completion.
//  - A set of (optional) push and pull promises to aid main. Push promises are
//    polled before main, pull promises are polled after. In this way we can
//    avoid overall wakeup churn - sending a message will tend to push things
//    down the promise tree as its polled, so that send should be in a push
//    promise - then as the main promise is polled and it calls into things
//    lower in the stack they'll already see things there (this reasoning holds
//    for receiving things and the pull promises too!).
//  - Each push and pull promise is either necessary or optional.
//    Necessary promises must complete successfully before the overall promise
//    completes. Optional promises will just be cancelled once the main promise
//    completes and any necessary helpers.
//  - If any of the promises fail, the overall promise fails immediately.
// API:
//  This function, TryConcurrently, is used to create a TryConcurrently promise.
//  It takes a single argument, being the main promise. That promise also has
//  a set of methods for attaching push and pull promises. The act of attachment
//  returns a new TryConcurrently promise with previous contained promises moved
//  out.
//  The methods exposed:
//  - Push, NecessaryPush: attach a push promise (with the first variant being
//                         optional, the second necessary).
//  - Pull, NecessaryPull: attach a pull promise, with variants as above.
// Example:
//  TryConcurrently(call_next_filter(std::move(call_args)))
//     .Push(send_messages_promise)
//     .Pull(recv_messages_promise)
template <typename Main>
auto TryConcurrently(Main main) {
  return promise_detail::MakeTryConcurrently(std::move(main),
                                             promise_detail::FusedSet<>(),
                                             promise_detail::FusedSet<>());
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_PROMISE_TRY_CONCURRENTLY_H
