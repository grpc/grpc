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

#ifndef GRPC_CORE_LIB_PROMISE_INTERCEPTOR_LIST_H
#define GRPC_CORE_LIB_PROMISE_INTERCEPTOR_LIST_H

#include "absl/types/optional.h"
#include "poll.h"

#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {

template <typename T>
class InterceptorList {
 public:
  class MapFactory {
   public:
    virtual void MakePromise(T x, void* memory) = 0;
    virtual void Destroy(void* memory) = 0;
    virtual Poll<absl::optional<T>> PollOnce(void* memory) = 0;
    virtual ~MapFactory() = default;

    void SetNext(MapFactory* next) {
      GPR_DEBUG_ASSERT(next_ == nullptr);
      next_ = next;
    }

    MapFactory* next() const { return next_; }

   private:
    MapFactory* next_ = nullptr;
  };

  class RunPromise {
   public:
    RunPromise(size_t memory_required, MapFactory* factory,
               absl::optional<T> value) {
      if (!value.has_value() || factory == nullptr) {
        is_running_ = false;
        Construct(&result_, std::move(value));
      } else {
        is_running_ = true;
        Construct(&running_, memory_required);
        factory->MakePromise(std::move(*value), running_.space.get());
        running_.current_factory = factory;
      }
    }

    ~RunPromise() {
      if (is_running_) {
        if (running_.current_factory != nullptr) {
          running_.current_factory->Destroy(running_.space.get());
        }
        Destruct(&running_);
      } else {
        Destruct(&result_);
      }
    }

    RunPromise(const RunPromise&) = delete;
    RunPromise& operator=(const RunPromise&) = delete;

    RunPromise(RunPromise&& other) noexcept : is_running_(other.is_running_) {
      if (is_running_) {
        Construct(&running_, std::move(other.running_));
      } else {
        Construct(&result_, std::move(other.result_));
      }
    }

    RunPromise& operator=(RunPromise&& other) noexcept = delete;

    Poll<absl::optional<T>> operator()() {
      while (is_running_) {
        auto r = running_.current_factory->PollOnce(running_.space.get());
        if (auto* p = absl::get_if<kPollReadyIdx>(&r)) {
          running_.current_factory->Destroy(running_.space.get());
          running_.current_factory = running_.current_factory->next();
          if (running_.current_factory == nullptr || !p->has_value()) {
            return std::move(*p);
          }
          running_.current_factory->MakePromise(std::move(**p),
                                                running_.space.get());
          continue;
        }
        return Pending{};
      }
      return std::move(result_);
    }

   private:
    struct Running {
      explicit Running(size_t max_size)
          : space(GetContext<Arena>()->MakePooledArray<char>(max_size)) {}
      Running(const Running&) = delete;
      Running& operator=(const Running&) = delete;
      Running(Running&& other) noexcept
          : current_factory(std::exchange(other.current_factory, nullptr)),
            space(std::move(other.space)) {}
      MapFactory* current_factory;
      Arena::PoolPtr<char[]> space;
    };
    union {
      Running running_;
      absl::optional<T> result_;
    };
    bool is_running_;
  };

  InterceptorList() = default;
  InterceptorList(const InterceptorList&) = delete;
  InterceptorList& operator=(const InterceptorList&) = delete;
  ~InterceptorList() {
    for (auto* f = first_map_; f != nullptr;) {
      auto* next = f->next();
      f->~MapFactory();
      f = next;
    }
  }

  RunPromise Run(absl::optional<T> initial_value) {
    return RunPromise(promise_memory_required_, first_map_,
                      std::move(initial_value));
  }

  template <typename Fn>
  void AppendMap(Fn fn) {
    Append(MakeFactoryToAdd(std::move(fn)));
  }

  template <typename Fn>
  void PrependMap(Fn fn) {
    Prepend(MakeFactoryToAdd(std::move(fn)));
  }

 private:
  template <typename Fn>
  class MapFactoryImpl final : public MapFactory {
   public:
    using PromiseFactory = promise_detail::RepeatedPromiseFactory<T, Fn>;
    using Promise = typename PromiseFactory::Promise;

    explicit MapFactoryImpl(Fn fn) : fn_(std::move(fn)) {}
    virtual void MakePromise(T x, void* memory) final {
      new (memory) Promise(fn_.Make(std::move(x)));
    }
    void Destroy(void* memory) { static_cast<Promise*>(memory)->~Promise(); }
    Poll<absl::optional<T>> PollOnce(void* memory) {
      return poll_cast<absl::optional<T>>((*static_cast<Promise*>(memory))());
    }

   private:
    PromiseFactory fn_;
  };

  template <typename Fn>
  MapFactory* MakeFactoryToAdd(Fn fn) {
    using FactoryType = MapFactoryImpl<Fn>;
    promise_memory_required_ = std::max(promise_memory_required_,
                                        sizeof(typename FactoryType::Promise));
    return GetContext<Arena>()->New<FactoryType>(std::move(fn));
  }

  void Append(MapFactory* f) {
    if (first_map_ == nullptr) {
      first_map_ = f;
      last_map_ = f;
    } else {
      last_map_->SetNext(f);
      last_map_ = f;
    }
  }

  void Prepend(MapFactory* f) {
    if (first_map_ == nullptr) {
      first_map_ = f;
      last_map_ = f;
    } else {
      f->SetNext(first_map_);
      first_map_ = f;
    }
  }

  MapFactory* first_map_ = nullptr;
  MapFactory* last_map_ = nullptr;
  size_t promise_memory_required_ = 0;
};

}  // namespace grpc_core

#endif
