// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CHANNELZ_ZTRACE_COLLECTOR_H
#define GRPC_SRC_CORE_CHANNELZ_ZTRACE_COLLECTOR_H

#include <grpc/support/time.h>

#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

#include "google/protobuf/any.upb.h"
#include "google/protobuf/any.upbdefs.h"
#include "google/protobuf/timestamp.upb.h"
#include "src/core/channelz/channelz.h"
#include "src/core/channelz/text_encode.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/util/function_signature.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/memory_usage.h"
#include "src/core/util/single_set_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"
#include "src/proto/grpc/channelz/v2/service.upb.h"
#include "upb/mem/arena.hpp"
#include "absl/container/flat_hash_set.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"

#ifdef GRPC_NO_ZTRACE
namespace grpc_core::channelz {
namespace ztrace_collector_detail {
class ZTraceImpl final : public ZTrace {
 public:
  explicit ZTraceImpl() {}

  void Run(Args args,
           std::shared_ptr<grpc_event_engine::experimental::EventEngine>
               event_engine,
           Callback callback) override {}
};

class StubImpl {
 public:
  template <typename T>
  void Append(const T&) {}

  std::unique_ptr<ZTrace> MakeZTrace() {
    return std::make_unique<ZTraceImpl>();
  }

  bool IsActive() { return false; }
};
}  // namespace ztrace_collector_detail

template <typename...>
class ZTraceCollector : public ztrace_collector_detail::StubImpl {};
}  // namespace grpc_core::channelz
#else
namespace grpc_core::channelz {
namespace ztrace_collector_detail {

template <typename T>
using Collection = std::deque<std::pair<gpr_cycle_counter, T> >;

template <typename T>
void AppendResults(const Collection<T>& data,
                   grpc_channelz_v2_QueryTraceResponse* response,
                   upb_Arena* arena) {
  size_t original_size;
  grpc_channelz_v2_QueryTraceResponse_events(response, &original_size);
  grpc_channelz_v2_TraceEvent** events =
      grpc_channelz_v2_QueryTraceResponse_resize_events(
          response, original_size + data.size(), arena);
  size_t event_index = original_size;
  for (const auto& value : data) {
    grpc_channelz_v2_TraceEvent* event = grpc_channelz_v2_TraceEvent_new(arena);
    events[event_index] = event;
    google_protobuf_Timestamp* timestamp =
        grpc_channelz_v2_TraceEvent_mutable_timestamp(event, arena);
    const gpr_timespec gpr_ts = gpr_convert_clock_type(
        gpr_cycle_counter_to_time(value.first), GPR_CLOCK_REALTIME);
    google_protobuf_Timestamp_set_seconds(timestamp, gpr_ts.tv_sec);
    google_protobuf_Timestamp_set_nanos(timestamp, gpr_ts.tv_nsec);
    grpc_channelz_v2_Data** data =
        grpc_channelz_v2_TraceEvent_resize_data(event, 1, arena);
    grpc_channelz_v2_Data* data_value = grpc_channelz_v2_Data_new(arena);
    data[0] = data_value;
    grpc_channelz_v2_Data_set_name(data_value,
                                   StdStringToUpbString(TypeName<T>()));
    value.second.ChannelzProperties().FillAny(
        grpc_channelz_v2_Data_mutable_value(data_value, arena), arena);
    ++event_index;
  }
}

template <typename Needle, typename... Haystack>
constexpr bool kIsElement = false;

template <typename Needle, typename... Haystack>
constexpr bool kIsElement<Needle, Needle, Haystack...> = true;

template <typename Needle, typename H, typename... Haystack>
constexpr bool kIsElement<Needle, H, Haystack...> =
    kIsElement<Needle, Haystack...>;

}  // namespace ztrace_collector_detail

inline std::optional<int64_t> IntFromArgs(const ZTrace::Args& args,
                                          const std::string& name) {
  auto it = args.find(name);
  if (it == args.end()) return std::nullopt;
  if (const int64_t* value = std::get_if<int64_t>(&it->second);
      value != nullptr) {
    return *value;
  }
  return std::nullopt;
}

// Generic collector infrastructure for ztrace queries.
// Abstracts away most of the ztrace requirements in an efficient manner,
// allowing system authors to concentrate on emitting useful data.
// If no trace is performed, overhead is one pointer and one relaxed atomic read
// per trace event.
//
// Two kinds of objects are required:
// 1. A `Config`
//    - This type should be constructible with a std::map<std::string,
//    std::string>
//      and provides overall query configuration - the map can be used to pull
//      predicates from the calling system.
//    - Needs a `bool Finishes(T)` method for each Data type (see 2).
//      This allows the config to terminate a query in the event of reaching
//      some configured predicate.
// 2. N `Data` types
//    - One for each kind of data captured in the trace
//    - Allows avoiding e.g. variant<> data types; these are inefficient
//      in this context because they force every recorded entry to use the
//      same number of bytes whilst pending.
template <typename Config, typename... Data>
class ZTraceCollector {
 public:
  // Append a value to any traces that are currently active.
  // If no trace is active, this is a no-op.
  // One can pass in the value to be appended, and that value will be used
  // directly.
  // Or one can pass in a producer - a lambda that will return the value to be
  // appended. This will only be called if the value is needed - so that we can
  // elide construction costs if the value is not traced.
  // Prefer the latter if there is an allocation for example, but if you're
  // tracing one int that's already on the stack then no need to inject more
  // complexity.
  template <typename X>
  void Append(X producer_or_value) {
    if constexpr (ztrace_collector_detail::kIsElement<X, Data...>) {
      GRPC_LATENT_SEE_ALWAYS_ON_MARK_EXTRA_EVENT(X, producer_or_value);
    } else {
      using ResultType = absl::result_of_t<decltype(producer_or_value)()>;
      GRPC_LATENT_SEE_ALWAYS_ON_MARK_EXTRA_EVENT(ResultType,
                                                 producer_or_value());
    }
    GRPC_TRACE_LOG(ztrace, INFO) << "ZTRACE[" << this << "]: " << [&]() {
      upb::Arena arena;
      google_protobuf_Any* any = google_protobuf_Any_new(arena.ptr());
      if constexpr (ztrace_collector_detail::kIsElement<X, Data...>) {
        producer_or_value.ChannelzProperties().FillAny(any, arena.ptr());
      } else {
        producer_or_value().ChannelzProperties().FillAny(any, arena.ptr());
      }
      return TextEncode(reinterpret_cast<upb_Message*>(any),
                        google_protobuf_Any_getmsgdef);
    }();
    if (!impl_.is_set()) return;
    if constexpr (ztrace_collector_detail::kIsElement<X, Data...>) {
      AppendValue(std::move(producer_or_value));
    } else {
      AppendValue(producer_or_value());
    }
  }

  // Try to avoid using this method!
  // Returns true if (instantaneously) there are any tracers active.
  // It's about as expensive as Append() so there's no point guarding Append()
  // with this. However, if you'd need to do a large amount of work perhaps
  // asynchronously before doing an Append, this can be useful to control that
  // work.
  bool IsActive() {
    if (!impl_.is_set()) return false;
    auto impl = impl_.Get();
    MutexLock lock(&impl->mu);
    return !impl->instances.empty();
  }

  std::unique_ptr<ZTrace> MakeZTrace() {
    return std::make_unique<ZTraceImpl>(impl_.GetOrCreate());
  }

 private:
  template <typename T>
  using Collection = ztrace_collector_detail::Collection<T>;

  class Instance : public RefCounted<Instance> {
   public:
    Instance(ZTrace::Args args,
             std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                 event_engine)
        : memory_cap_(IntFromArgs(args, "memory_cap").value_or(1024 * 1024)),
          config_(std::move(args)),
          event_engine_(std::move(event_engine)) {}
    using Collections = std::tuple<Collection<Data>...>;
    template <typename T>
    void Append(std::pair<gpr_cycle_counter, T> value) {
      switch (state_) {
        case State::kIdle:
        case State::kReady:
          state_ = State::kReady;
          break;
        case State::kReadyDone:
        case State::kDone:
          return;
      }
      if (state_ == State::kDone) return;
      ++items_matched_;
      memory_used_ += MemoryUsageOf(value.second);
      while (memory_used_ > memory_cap_) {
        auto memory_used_before = memory_used_;
        RemoveMostRecent();
        CHECK_LT(memory_used_, memory_used_before);
      }
      std::get<Collection<T> >(data_).push_back(std::move(value));
      if (callback_ != nullptr) QueueCallback();
    }

    template <typename T>
    bool Finishes(const T& value) {
      return config_.Finishes(value);
    }

    void Finish(absl::Status status) {
      switch (state_) {
        case State::kIdle:
          state_ = State::kDone;
          break;
        case State::kReady:
          state_ = State::kReadyDone;
          break;
        case State::kReadyDone:
        case State::kDone:
          return;
      }
      GRPC_TRACE_LOG(ztrace, INFO) << "ZTRACE[" << this << "]: Finish";
      status_ = std::move(status);
      if (callback_ != nullptr) QueueCallback();
    }

    void Next(ZTrace::Callback callback) {
      callback_ = std::move(callback);
      if (state_ != State::kIdle) QueueCallback();
    }

    Timestamp start_time() const { return start_time_; }

   private:
    enum class State {
      kIdle,
      kReady,
      kReadyDone,
      kDone,
    };

    struct RemoveMostRecentState {
      void (*enact)(Instance*) = nullptr;
      gpr_cycle_counter most_recent =
          std::numeric_limits<gpr_cycle_counter>::max();
    };

    void QueueCallback() {
      switch (state_) {
        case State::kIdle:
          LOG(FATAL) << "BUG: kIdle";
          break;
        case State::kReady:
          QueueCallbackReady();
          state_ = State::kIdle;
          break;
        case State::kReadyDone:
          QueueCallbackReady();
          state_ = State::kDone;
          break;
        case State::kDone:
          QueueCallbackDone();
          break;
      }
    }

    void QueueCallbackReady() {
      Collections data = std::move(data_);
      size_t items_matched = std::exchange(items_matched_, 0);
      memory_used_ = 0;
      event_engine_->Run(
          [data = std::move(data), items_matched,
           callback = std::exchange(callback_, nullptr)]() mutable {
            upb::Arena arena;
            grpc_channelz_v2_QueryTraceResponse* response =
                grpc_channelz_v2_QueryTraceResponse_new(arena.ptr());
            grpc_channelz_v2_QueryTraceResponse_set_num_events_matched(
                response, items_matched);
            (ztrace_collector_detail::AppendResults(
                 std::get<Collection<Data> >(data), response, arena.ptr()),
             ...);
            size_t len = 0;
            char* serialized = grpc_channelz_v2_QueryTraceResponse_serialize(
                response, arena.ptr(), &len);
            callback(std::string(serialized, len));
          });
    }

    void QueueCallbackDone() {
      event_engine_->Run(
          [callback = std::exchange(callback_, nullptr),
           status = std::exchange(status_, absl::Status())]() mutable {
            if (status.ok()) {
              callback(std::nullopt);
            } else {
              callback(status);
            }
          });
    }

    void RemoveMostRecent() {
      RemoveMostRecentState state;
      (UpdateRemoveMostRecentState<Data>(&state), ...);
      CHECK(state.enact != nullptr);
      state.enact(this);
    }
    template <typename T>
    void UpdateRemoveMostRecentState(RemoveMostRecentState* state) {
      auto& collection = std::get<Collection<T> >(data_);
      if (collection.empty()) return;
      if (state->enact == nullptr ||
          collection.front().first < state->most_recent) {
        state->enact = +[](Instance* instance) {
          auto& collection = std::get<Collection<T> >(instance->data_);
          const size_t ent_usage = MemoryUsageOf(collection.front().second);
          CHECK_GE(instance->memory_used_, ent_usage);
          instance->memory_used_ -= ent_usage;
          collection.pop_front();
        };
        state->most_recent = collection.front().first;
      }
    }

    const Timestamp start_time_ = Timestamp::Now();
    size_t memory_used_ = 0;
    size_t memory_cap_ = 0;
    uint64_t items_matched_ = 0;
    State state_ = State::kIdle;
    Config config_;
    Collections data_;
    absl::Status status_;
    ZTrace::Callback callback_;
    const std::shared_ptr<grpc_event_engine::experimental::EventEngine>
        event_engine_;
  };
  struct Impl : public RefCounted<Impl> {
    Mutex mu;
    absl::flat_hash_set<RefCountedPtr<Instance> > instances ABSL_GUARDED_BY(mu);
  };
  class ZTraceImpl final : public ZTrace {
   public:
    explicit ZTraceImpl(RefCountedPtr<Impl> impl) : impl_(std::move(impl)) {}

    ~ZTraceImpl() override {
      if (instance_ != nullptr) {
        MutexLock lock(&impl_->mu);
        instance_->Finish(absl::CancelledError());
      }
    }

    void Run(ZTrace::Args args,
             std::shared_ptr<grpc_event_engine::experimental::EventEngine>
                 event_engine,
             ZTrace::Callback callback) override {
      CHECK(instance_ == nullptr);
      instance_ = MakeRefCounted<Instance>(std::move(args), event_engine);
      RefCountedPtr<Instance> oldest_instance;
      MutexLock lock(&impl_->mu);
      if (impl_->instances.size() > 20) {
        // Eject oldest running trace
        Timestamp oldest_time = Timestamp::InfFuture();
        for (auto& instance : impl_->instances) {
          if (instance->start_time() < oldest_time) {
            oldest_time = instance->start_time();
            oldest_instance = instance;
          }
        }
        CHECK(oldest_instance != nullptr);
        impl_->instances.erase(oldest_instance);
        oldest_instance->Finish(
            absl::ResourceExhaustedError("Too many concurrent ztrace queries"));
      }
      impl_->instances.insert(instance_);
      NextCallback(std::make_shared<Callback>(std::move(callback)), impl_,
                   instance_);
    }

   private:
    static void NextCallback(std::shared_ptr<ZTrace::Callback> callback,
                             RefCountedPtr<Impl> impl,
                             RefCountedPtr<Instance> instance) {
      instance->Next([callback = std::move(callback), impl = std::move(impl),
                      instance = std::move(instance)](
                         absl::StatusOr<std::optional<std::string> > response) {
        const bool end =
            (response.ok() && !response->has_value()) || !response.ok();
        (*callback)(std::move(response));
        MutexLock lock(&impl->mu);
        if (end) {
          impl->instances.erase(instance);
        } else {
          NextCallback(std::move(callback), std::move(impl),
                       std::move(instance));
        }
      });
    }

    const RefCountedPtr<Impl> impl_;
    RefCountedPtr<Instance> instance_;
  };

  template <typename T>
  void AppendValue(T&& data) {
    auto value = std::pair(gpr_get_cycle_counter(), std::forward<T>(data));
    auto* impl = impl_.Get();
    {
      MutexLock lock(&impl->mu);
      switch (impl->instances.size()) {
        case 0:
          return;
        case 1: {
          auto& instances = impl->instances;
          auto& instance = *instances.begin();
          const bool finishes = instance->Finishes(value.second);
          instance->Append(std::move(value));
          if (finishes) {
            instance->Finish(absl::OkStatus());
            instances.clear();
          }
        } break;
        default: {
          std::vector<RefCountedPtr<Instance> > finished;
          for (auto& instance : impl->instances) {
            const bool finishes = instance->Finishes(value.second);
            instance->Append(value);
            if (finishes) {
              finished.push_back(instance);
            }
          }
          for (const auto& instance : finished) {
            instance->Finish(absl::OkStatus());
            impl->instances.erase(instance);
          }
        }
      }
    }
  }

  SingleSetRefCountedPtr<Impl> impl_;
};
}  // namespace grpc_core::channelz
#endif  // GRPC_NO_ZTRACE

#endif  // GRPC_SRC_CORE_CHANNELZ_ZTRACE_COLLECTOR_H
