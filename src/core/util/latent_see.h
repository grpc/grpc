// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_UTIL_LATENT_SEE_H
#define GRPC_SRC_CORE_UTIL_LATENT_SEE_H

#include <grpc/support/port_platform.h>
#include <grpc/support/thd_id.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "src/core/channelz/property_list.h"
#include "src/core/util/alloc.h"
#include "src/core/util/function_signature.h"
#include "src/core/util/mpscq.h"
#include "src/core/util/notification.h"
#include "src/core/util/thd.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"

#define GRPC_EXTRA_LATENT_SEE 1

namespace grpc_core {
namespace latent_see {

class Output {
 public:
  virtual void Mark(absl::string_view name, int64_t tid, int64_t timestamp,
                    channelz::PropertyList property_list) = 0;
  virtual void FlowBegin(absl::string_view name, int64_t tid, int64_t timestamp,
                         int64_t flow_id) = 0;
  virtual void FlowEnd(absl::string_view name, int64_t tid, int64_t timestamp,
                       int64_t flow_id) = 0;
  virtual void Span(absl::string_view name, int64_t tid,
                    int64_t timestamp_begin, int64_t duration) = 0;
  virtual void Finish() = 0;

 protected:
  ~Output() = default;
};

class DiscardOutput final : public Output {
 public:
  void Mark(absl::string_view, int64_t, int64_t,
            channelz::PropertyList) override {}
  void FlowBegin(absl::string_view, int64_t, int64_t, int64_t) override {}
  void FlowEnd(absl::string_view, int64_t, int64_t, int64_t) override {}
  void Span(absl::string_view, int64_t, int64_t, int64_t) override {}
  void Finish() override {}
};

class JsonOutput final : public Output {
 public:
  explicit JsonOutput(std::ostream& out) : out_(out) { out_ << "[\n"; }

  void Mark(absl::string_view name, int64_t tid, int64_t timestamp,
            channelz::PropertyList property_list) override;
  void FlowBegin(absl::string_view name, int64_t tid, int64_t timestamp,
                 int64_t flow_id) override;
  void FlowEnd(absl::string_view name, int64_t tid, int64_t timestamp,
               int64_t flow_id) override;
  void Span(absl::string_view name, int64_t tid, int64_t timestamp_begin,
            int64_t duration) override;
  void Finish() override;

 private:
  static std::string MicrosString(int64_t nanos);

  std::ostream& out_;
  const char* sep_ = "";
};

}  // namespace latent_see
}  // namespace grpc_core

#ifndef GRPC_DISABLE_LATENT_SEE

namespace grpc_core {
namespace latent_see {

struct Metadata {
  const char* file;
  int line;
  absl::string_view name;
  int extra_event_size;
  void (*destruct_extra_event)(void* extra_event);
  channelz::PropertyList (*to_property_list)(void* extra_event);
};

// A bin collects all events that occur within a parent scope.
struct Bin : public MultiProducerSingleConsumerQueue::Node {
  struct alignas(GPR_MAX_ALIGNMENT) Event {
    const Metadata* metadata;
    int64_t timestamp_begin;
    int64_t timestamp_end;

    static constexpr uint64_t kSpan = 0;
    static constexpr uint64_t kFlow = 1;
  };
  enum class AppendResult : uint8_t {
    kSuccess = 0,
    kFailed = 1,
    kFull = 2,
  };
  class Iterator {
   public:
    // Type definitions required by STL
    using value_type = Event;
    using pointer = Event*;
    using reference = Event&;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;  // Or other categories

    // Constructor
    explicit Iterator(const uint8_t* ptr) : ptr_(ptr) {}

    // Dereference operator
    const Event& operator*() const {
      return *reinterpret_cast<const Event*>(ptr_);
    }
    const Event* operator->() const {
      return reinterpret_cast<const Event*>(ptr_);
    }

    uint8_t* ptr() const { return const_cast<uint8_t*>(ptr_); }

    // Increment operators
    Iterator& operator++() {  // Pre-increment
      const Event* curr_event = reinterpret_cast<const Event*>(ptr_);
      if (curr_event->metadata->extra_event_size == 0) {
        ptr_ += sizeof(Event);
      } else {
        ptr_ += curr_event->metadata->extra_event_size + sizeof(Event);
      }
      return *this;
    }
    Iterator operator++(int) {  // Post-increment
      Iterator temp = *this;
      operator++();
      return temp;
    }

    // Comparison operators
    bool operator==(const Iterator& other) const { return ptr_ == other.ptr_; }
    bool operator!=(const Iterator& other) const { return ptr_ != other.ptr_; }

   private:
    const uint8_t* ptr_;
  };

  AppendResult Append(const Metadata* metadata, int64_t timestamp_begin,
                      int64_t timestamp_end) {
    Event* ev = reinterpret_cast<Event*>(&events[used_space]);
    if (used_space + sizeof(Event) > kMaxBinSize) {
      // No space left in the bin to append the event.
      return AppendResult::kFailed;
    }
    ev->metadata = metadata;
    ev->timestamp_begin = timestamp_begin;
    ev->timestamp_end = timestamp_end;
    used_space += sizeof(Event);
    return used_space == kMaxBinSize ? AppendResult::kFull
                                     : AppendResult::kSuccess;
  }

  template <typename X>
  AppendResult Append(const Metadata* metadata, int64_t timestamp_begin,
                      int64_t timestamp_end, const X& x) {
    if (used_space + sizeof(Event) +
            GPR_ROUND_UP_TO_ALIGNMENT_SIZE(metadata->extra_event_size) >
        kMaxBinSize) {
      // No space left in the bin to append this event.
      return AppendResult::kFailed;
    }
    uint8_t* ptr = &events[used_space];
    // Copy construct the extra_event in the space immediately after this
    // Event struct.
    new (reinterpret_cast<void*>(ptr + sizeof(Event))) X(x);
    Event* ev = reinterpret_cast<Event*>(ptr);
    ev->metadata = metadata;
    ev->timestamp_begin = timestamp_begin;
    ev->timestamp_end = timestamp_end;
    used_space += sizeof(Event) + metadata->extra_event_size;
    return used_space == kMaxBinSize ? AppendResult::kFull
                                     : AppendResult::kSuccess;
  }

  ~Bin() {
    for (auto it = this->begin(); it != this->end(); ++it) {
      if (it->metadata->extra_event_size > 0) {
        it->metadata->destruct_extra_event(it.ptr() + sizeof(Event));
      }
    }
  }

  Iterator begin() const { return Iterator(&events[0]); }
  Iterator end() const { return Iterator(&events[used_space]); }
  static constexpr size_t kMaxBinSize = 8192;
  static constexpr size_t kEventsPerBin = kMaxBinSize / sizeof(Event);
  size_t used_space = 0;
  gpr_thd_id thd_id = gpr_thd_currentid();
  alignas(Event) uint8_t events[kMaxBinSize];
};

void Collect(Notification* notification, absl::Duration timeout,
             size_t memory_limit, Output* output);

class Sink {
 public:
  using EventDump = std::deque<std::unique_ptr<Bin>>;

  Sink();
  ~Sink() = delete;
  void Append(std::unique_ptr<Bin> bin);

 private:
  friend void Collect(Notification*, absl::Duration, size_t, Output*);

  void Gather();
  void Record(std::unique_ptr<Bin> bin);

  void Start(size_t max_bins);
  std::unique_ptr<EventDump> Stop();

  MultiProducerSingleConsumerQueue appending_;
  Thread gatherer_;
  Mutex mu_;
  std::unique_ptr<EventDump> events_ ABSL_GUARDED_BY(mu_);
  size_t max_bins_ ABSL_GUARDED_BY(mu_);
};

class Appender {
 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Appender()
      : Appender(active_sink_.load(std::memory_order_acquire)) {};
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit Appender(Sink* sink)
      : sink_(sink) {}
  Appender(const Appender&) = delete;
  Appender& operator=(const Appender&) = delete;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool Enabled() const {
    return sink_ != nullptr;
  }
  void Append(const Metadata* metadata, int64_t timestamp_begin,
              int64_t timestamp_end) {
    DCHECK(Enabled());
    DCHECK_NE(metadata, nullptr);
    Bin::AppendResult result;
    do {
      if (GPR_UNLIKELY(bin_ == nullptr)) bin_ = std::make_unique<Bin>();
      result = bin_->Append(metadata, timestamp_begin, timestamp_end);
      if (GPR_UNLIKELY(result == Bin::AppendResult::kFailed)) {
        sink_->Append(std::move(bin_));
        // Retry with a new bin.
        bin_ = nullptr;
      }
    } while (result == Bin::AppendResult::kFailed);

    if (GPR_UNLIKELY(result == Bin::AppendResult::kFull)) {
      sink_->Append(std::move(bin_));
    }
  }
  template <typename X>
  void Append(const Metadata* metadata, int64_t timestamp_begin,
              int64_t timestamp_end, const X& x) {
    DCHECK(Enabled());
    DCHECK_NE(metadata, nullptr);
    Bin::AppendResult result;
    do {
      if (GPR_UNLIKELY(bin_ == nullptr)) bin_ = std::make_unique<Bin>();
      result = bin_->Append(metadata, timestamp_begin, timestamp_end, x);
      if (GPR_UNLIKELY(result == Bin::AppendResult::kFailed)) {
        sink_->Append(std::move(bin_));
        // Retry with a new bin.
        bin_ = nullptr;
      }
    } while (result == Bin::AppendResult::kFailed);

    if (GPR_UNLIKELY(result == Bin::AppendResult::kFull)) {
      sink_->Append(std::move(bin_));
    }
  }

  void Flush() {
    if (GPR_UNLIKELY(bin_ != nullptr)) sink_->Append(std::move(bin_));
  }

 private:
  friend void Collect(Notification*, absl::Duration, size_t, Output*);

  static void Enable(Sink* sink);
  static void Disable();

  Sink* sink_;
  static thread_local std::unique_ptr<Bin> bin_;
  static std::atomic<Sink*> active_sink_;
};

inline void Flush() {
  Appender appender;
  if (GPR_UNLIKELY(!appender.Enabled())) return;
  appender.Flush();
}

class Scope final {
 public:
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit Scope(
      const Metadata* metadata) {
    if (GPR_LIKELY(!appender_.Enabled())) return;
    metadata_ = metadata;
    timestamp_begin_ = absl::GetCurrentTimeNanos();
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION ~Scope() {
    if (GPR_LIKELY(!appender_.Enabled())) return;
    appender_.Append(metadata_, timestamp_begin_, absl::GetCurrentTimeNanos());
  }

 private:
  Appender appender_;
  int64_t timestamp_begin_;
  const Metadata* metadata_;
};

GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static inline void Mark(
    const Metadata* metadata) {
  Appender appender;
  if (GPR_LIKELY(!appender.Enabled())) return;
  const auto ts = absl::GetCurrentTimeNanos();
  appender.Append(metadata, ts, ts);
}

template <typename X>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION static inline void MarkExtraEvent(
    const Metadata* metadata, const X& x) {
  Appender appender;
  if (GPR_LIKELY(!appender.Enabled())) return;
  const auto ts = absl::GetCurrentTimeNanos();
  appender.Append(metadata, ts, ts, x);
}

class Flow {
 public:
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION Flow() : id_(0) {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION explicit Flow(const Metadata* metadata) {
    Appender appender;
    metadata_ = metadata;
    if (GPR_LIKELY(!appender.Enabled())) {
      id_ = 0;
      return;
    }
    AppendBegin(appender);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION ~Flow() {
    if (GPR_LIKELY(id_ == 0)) return;
    Appender appender;
    if (GPR_LIKELY(!appender.Enabled())) return;
    AppendEnd(appender);
  }

  Flow(const Flow&) = delete;
  Flow& operator=(const Flow&) = delete;
  Flow(Flow&& other) noexcept
      : metadata_(other.metadata_), id_(std::exchange(other.id_, 0)) {}
  Flow& operator=(Flow&& other) noexcept {
    metadata_ = other.metadata_;
    if (id_ != 0) {
      Appender appender;
      if (GPR_LIKELY(!appender.Enabled())) AppendEnd(appender);
    }
    id_ = std::exchange(other.id_, 0);
    return *this;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool is_active() const {
    return id_ != 0;
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void End() {
    if (GPR_LIKELY(id_ == 0)) return;
    Appender appender;
    if (GPR_LIKELY(!appender.Enabled())) return;
    AppendEnd(appender);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void Begin(const Metadata* metadata) {
    Appender appender;
    if (GPR_LIKELY(!appender.Enabled())) return;
    if (id_ != 0) AppendEnd(appender);
    metadata_ = metadata;
    AppendBegin(appender);
  }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void Begin() {
    DCHECK(metadata_ != nullptr);
    Begin(metadata_);
  }

 private:
  void AppendBegin(Appender& appender) {
    id_ = next_id_.fetch_add(1, std::memory_order_relaxed);
    appender.Append(metadata_, -id_, absl::GetCurrentTimeNanos());
  }
  void AppendEnd(Appender& appender) {
    DCHECK_NE(id_, 0);
    appender.Append(metadata_, -id_, -absl::GetCurrentTimeNanos());
    id_ = 0;
  }
  const Metadata* metadata_ = nullptr;
  static inline std::atomic<int64_t> next_id_{1};
  int64_t id_;
};

template <typename P>
GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION auto Promise(const Metadata* md_poll,
                                                  const Metadata* md_flow,
                                                  P promise) {
  return
      [md_poll, promise = std::move(promise), flow = Flow(md_flow)]() mutable {
        Scope scope(md_poll);
        flow.End();
        auto r = promise();
        if (IsPending(r)) flow.Begin();
        return r;
      };
}

}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_SYMBOL2(name, line) name##_##line
#define GRPC_LATENT_SEE_SYMBOL1(name, line) GRPC_LATENT_SEE_SYMBOL2(name, line)
#define GRPC_LATENT_SEE_SYMBOL(name) GRPC_LATENT_SEE_SYMBOL1(name, __LINE__)
#define GRPC_LATENT_SEE_METADATA(name)                  \
  []() {                                                \
    static grpc_core::latent_see::Metadata metadata = { \
        __FILE__, __LINE__, name, 0, nullptr, nullptr}; \
    return &metadata;                                   \
  }()
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define GRPC_LATENT_SEE_EXTRA_EVENT_METADATA(type)                           \
  []() {                                                                     \
    static grpc_core::latent_see::Metadata metadata = {                      \
        __FILE__,                                                            \
        __LINE__,                                                            \
        TypeName<type>(),                                                    \
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(type)),                        \
        [](void* extra_event) {                                              \
          /* NOLINTNEXTLINE(bugprone-macro-parentheses)  */                  \
          reinterpret_cast<type*>(extra_event)->~type();                     \
        },                                                                   \
        [](void* extra_event) {                                              \
          /* NOLINTNEXTLINE(bugprone-macro-parentheses)  */                  \
          return reinterpret_cast<type*>(extra_event)->ChannelzProperties(); \
        }};                                                                  \
    return &metadata;                                                        \
  }()
// Scope: marks a begin/end event in the log.
#define GRPC_LATENT_SEE_ALWAYS_ON_SCOPE(name)                            \
  grpc_core::latent_see::Scope GRPC_LATENT_SEE_SYMBOL(latent_see_scope)( \
      GRPC_LATENT_SEE_METADATA(name))
// Mark: logs a single event.
#define GRPC_LATENT_SEE_ALWAYS_ON_MARK(name) \
  grpc_core::latent_see::Mark(GRPC_LATENT_SEE_METADATA(name))
#define GRPC_LATENT_SEE_ALWAYS_ON_MARK_EXTRA_EVENT(type, extra_event) \
  grpc_core::latent_see::MarkExtraEvent(                              \
      GRPC_LATENT_SEE_EXTRA_EVENT_METADATA(type), extra_event)
#define GRPC_LATENT_SEE_ALWAYS_ON_PROMISE(name, promise)                      \
  grpc_core::latent_see::Promise(                                             \
      GRPC_LATENT_SEE_METADATA("Poll:" name), GRPC_LATENT_SEE_METADATA(name), \
      [&]() {                                                                 \
        GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("Setup:" name);                       \
        return promise;                                                       \
      }())
#ifdef GRPC_EXTRA_LATENT_SEE
#define GRPC_LATENT_SEE_SCOPE(name) GRPC_LATENT_SEE_ALWAYS_ON_SCOPE(name)
#define GRPC_LATENT_SEE_MARK(name) GRPC_LATENT_SEE_ALWAYS_ON_MARK(name)
#define GRPC_LATENT_SEE_MARK_EXTRA_EVENT(type, extra_event) \
  GRPC_LATENT_SEE_ALWAYS_ON_MARK_EXTRA_EVENT(type, extra_event)
#define GRPC_LATENT_SEE_PROMISE(name, promise) \
  GRPC_LATENT_SEE_ALWAYS_ON_PROMISE(name, promise)
#else
#define GRPC_LATENT_SEE_SCOPE(name) \
  do {                              \
  } while (0)
#define GRPC_LATENT_SEE_MARK(name) \
  do {                             \
  } while (0)
#define GRPC_LATENT_SEE_MARK_EXTRA_EVENT(type, extra_event) \
  do {                                                      \
  } while (0)
#define GRPC_LATENT_SEE_PROMISE(name, promise) promise
#endif
#else  // def(GRPC_DISABLE_LATENT_SEE)
namespace grpc_core {
namespace latent_see {
struct Metadata {};
struct Flow {
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool is_active() const { return false; }
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void End() {}
  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void Begin(Metadata*) {}
};
struct Scope {
  explicit Scope(Metadata*) {}
};

inline void Collect(Notification*, absl::Duration, size_t, Output* output) {
  output->Finish();
}
}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_METADATA(name) nullptr
#define GRPC_LATENT_SEE_METADATA_RAW(name) nullptr
#define GRPC_LATENT_SEE_ALWAYS_ON_SCOPE(name) \
  do {                                        \
  } while (0)
#define GRPC_LATENT_SEE_ALWAYS_ON_MARK(name) \
  do {                                       \
  } while (0)
#define GRPC_LATENT_SEE_ALWAYS_ON_MARK_EXTRA_EVENT(type, extra_event) \
  do {                                                                \
  } while (0)
#define GRPC_LATENT_SEE_ALWAYS_ON_PROMISE(name, promise) promise
#define GRPC_LATENT_SEE_SCOPE(name) \
  do {                              \
  } while (0)
#define GRPC_LATENT_SEE_MARK(name) \
  do {                             \
  } while (0)
#define GRPC_LATENT_MARK_EXTRA_EVENT(type, extra_event) \
  do {                                                  \
  } while (0)
#define GRPC_LATENT_SEE_PROMISE(name, promise) promise
#endif  // GRPC_DISABLE_LATENT_SEE

#endif  // GRPC_SRC_CORE_UTIL_LATENT_SEE_H
