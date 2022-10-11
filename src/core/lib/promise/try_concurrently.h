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

#include <cstdint>
#include <tuple>

#include "poll.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/bitset.h"
#include "src/core/lib/gprpp/construct_destruct.h"
#include "src/core/lib/promise/detail/promise_like.h"
#include "src/core/lib/promise/detail/status.h"

namespace grpc_core {

namespace promise_detail {

template <typename... Ts>
struct List;

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

template <int kIdx, int kDoneBit, typename... Ts>
struct RunTheThings;

template <int kIdx, int kDoneBit, typename T, typename... Ts>
struct RunTheThings<kIdx, kDoneBit, T, Ts...> {
  template <typename Result, typename Tpl>
  static Poll<Result> Run(uint8_t& done_bits, Tpl& ts) {
    if ((done_bits & (1 << kDoneBit)) == 0) {
      auto& t = std::get<kIdx>(ts);
      auto p = t.promise();
      if (auto* status = absl::get_if<kPollReadyIdx>(&p)) {
        if (IsStatusOk(*status)) {
          done_bits |= (1 << kDoneBit);
        } else {
          return Result(std::move(*status));
        }
      }
    }
    return RunTheThings<kIdx + 1, kDoneBit + 1, Ts...>::template Run<Result>(
        done_bits, ts);
  }
};

template <int kIdx, int kDoneBit>
struct RunTheThings<kIdx, kDoneBit> {
  template <typename Result, typename Tpl>
  static Poll<Result> Run(uint8_t& done_bits, Tpl& ts) {
    return Pending();
  }
};

template <typename... Ts>
struct NecessaryBits;

template <typename T, typename... Ts>
struct NecessaryBits<T, Ts...> {
  static constexpr uint8_t value =
      (T::must_complete() ? 1 : 0) | (NecessaryBits<Ts...>::value << 1);
};

template <>
struct NecessaryBits<> {
  static constexpr uint8_t value = 0;
};

template <typename Main, typename PreMain, typename PostMain>
class TryConcurrently;

template <typename Main, typename... PreMain, typename... PostMain>
class TryConcurrently<Main, List<PreMain...>, List<PostMain...>> {
 public:
  TryConcurrently(Main main, std::tuple<PreMain...> pre_main,
                  std::tuple<PostMain...> post_main)
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
    GPR_ASSERT(other.done_bits_ == 0);
    Construct(&main_, std::move(other.main_));
  }
  TryConcurrently& operator=(TryConcurrently&& other) noexcept {
    GPR_ASSERT(other.done_bits_ == 0);
    done_bits_ = 0;
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
  }

  using Result =
      typename PollTraits<decltype(std::declval<PromiseLike<Main>>()())>::Type;

  Poll<Result> operator()() {
    auto r = RunTheThings<0, 1, PreMain...>::template Run<Result>(done_bits_,
                                                                  pre_main_);
    if (auto* status = absl::get_if<Result>(&r)) {
      GPR_ASSERT(!IsStatusOk(*status));
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
    r = RunTheThings<0, 1 + sizeof...(PreMain),
                     PostMain...>::template Run<Result>(done_bits_, post_main_);
    if (auto* status = absl::get_if<Result>(&r)) {
      GPR_ASSERT(!IsStatusOk(*status));
      return std::move(*status);
    }
    static const uint8_t kNecessaryBits =
        1 | (NecessaryBits<PreMain...>::value << 1) |
        (NecessaryBits<PostMain...>::value << (1 + sizeof...(PreMain)));
    printf("done_bits=%d necessary_bits=%d\n", done_bits_, kNecessaryBits);
    if ((done_bits_ & kNecessaryBits) == kNecessaryBits) {
      return std::move(result_);
    }
    return Pending{};
  }

  template <typename P>
  TryConcurrently<Main, List<PreMain..., Necessary<P>>, List<PostMain...>>
  NecessaryPush(P p) {
    GPR_DEBUG_ASSERT(done_bits_ == 0);
    return TryConcurrently<Main, List<PreMain..., Necessary<P>>,
                           List<PostMain...>>(
        std::move(main_),
        std::tuple_cat(std::move(pre_main_),
                       std::make_tuple(Necessary<P>{std::move(p)})),
        std::move(post_main_));
  }

  template <typename P>
  TryConcurrently<Main, List<PreMain...>, List<PostMain..., Necessary<P>>>
  NecessaryPull(P p) {
    GPR_DEBUG_ASSERT(done_bits_ == 0);
    return TryConcurrently<Main, List<PreMain...>,
                           List<PostMain..., Necessary<P>>>(
        std::move(main_), std::move(pre_main_),
        std::tuple_cat(std::move(post_main_),
                       std::make_tuple(Necessary<P>{std::move(p)})));
  }

  template <typename P>
  TryConcurrently<Main, List<PreMain..., Helper<P>>, List<PostMain...>>
  HelperPush(P p) {
    GPR_DEBUG_ASSERT(done_bits_ == 0);
    return TryConcurrently<Main, List<PreMain..., Helper<P>>,
                           List<PostMain...>>(
        std::move(main_),
        std::tuple_cat(std::move(pre_main_),
                       std::make_tuple(Helper<P>{std::move(p)})),
        std::move(post_main_));
  }

  template <typename P>
  TryConcurrently<Main, List<PreMain...>, List<PostMain..., Helper<P>>>
  HelperPull(P p) {
    GPR_DEBUG_ASSERT(done_bits_ == 0);
    return TryConcurrently<Main, List<PreMain...>,
                           List<PostMain..., Helper<P>>>(
        std::move(main_), std::move(pre_main_),
        std::tuple_cat(std::move(post_main_),
                       std::make_tuple(Helper<P>{std::move(p)})));
  }

 private:
  uint8_t done_bits_;
  std::tuple<PreMain...> pre_main_;
  union {
    Main main_;
    Result result_;
  };
  std::tuple<PostMain...> post_main_;
};

}  // namespace promise_detail

template <typename Main>
auto TryConcurrently(Main main) {
  return promise_detail::TryConcurrently<promise_detail::PromiseLike<Main>,
                                         promise_detail::List<>,
                                         promise_detail::List<>>(
      std::move(main), std::make_tuple(), std::make_tuple());
}

}  // namespace grpc_core

#endif
