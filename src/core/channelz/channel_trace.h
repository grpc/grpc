//
//
// Copyright 2017 gRPC authors.
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
//
//

#ifndef GRPC_SRC_CORE_CHANNELZ_CHANNEL_TRACE_H
#define GRPC_SRC_CORE_CHANNELZ_CHANNEL_TRACE_H

#include <grpc/slice.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/time.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <algorithm>
#include <memory>
#include <tuple>
#include <type_traits>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/debug/trace_flags.h"
#include "src/core/util/json/json.h"
#include "src/core/util/memory_usage.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"

namespace grpc_core {
namespace channelz {

namespace testing {
size_t GetSizeofTraceEvent(void);
}

namespace detail {

class Renderer {
 public:
  virtual ~Renderer() = default;
  virtual std::string Render() const = 0;
  virtual size_t MemoryUsage() const = 0;
};

struct StrCatFn {
  template <typename... Arg>
  std::string operator()(const Arg&... args) {
    return absl::StrCat(args...);
  }
};

template <typename A>
auto AdaptForStorage(A&& a) {
  using RawA = std::remove_reference_t<A>;
  if constexpr (std::is_same_v<std::decay_t<RawA>, const char*>) {
    return absl::string_view(a);
  } else {
    return RawA(std::forward<A>(a));
  }
}

template <typename... Args>
std::unique_ptr<Renderer> RendererFromConcatenationInner(Args&&... args) {
  class R final : public Renderer {
   public:
    explicit R(Args&&... args) : args_(std::forward<Args>(args)...) {}

    std::string Render() const override {
      return std::apply(StrCatFn(), args_);
    }
    size_t MemoryUsage() const override {
      return MemoryUsageOf(args_) + sizeof(Renderer);
    }

   private:
    std::tuple<Args...> args_;
  };
  return std::make_unique<R>(std::forward<Args>(args)...);
}

template <typename... Args>
std::unique_ptr<Renderer> RendererFromConcatenation(Args&&... args) {
  return RendererFromConcatenationInner(
      AdaptForStorage<Args>(std::forward<Args>(args))...);
}

struct RendererFromConcatenationFn {
  template <typename... Args>
  auto operator()(Args&&... args) {
    return RendererFromConcatenation(std::forward<Args>(args)...);
  }
};

template <typename N>
void OutputLogFromLogExpr(N* out, std::unique_ptr<Renderer> renderer) {
  out->NewNode(std::move(renderer)).Commit();
}

template <typename N, typename... T>
class LogExpr {
 public:
  explicit LogExpr(N* node, T&&... values)
      : out_(node), values_(std::forward<T>(values)...) {}

  ~LogExpr() {
    if (out_ != nullptr) {
      OutputLogFromLogExpr(out_,
                           std::apply(detail::RendererFromConcatenationFn(),
                                      std::move(values_)));
    }
  }

  template <typename U>
  friend auto operator<<(LogExpr<N, T...>&& x, U&& u) {
    auto mk = [out = std::exchange(x.out_, nullptr),
               u = AdaptForStorage(std::forward<U>(u))](
                  T&&... existing_values) mutable {
      return LogExpr<N, T..., decltype(u)>(
          out, std::forward<T>(existing_values)..., std::move(u));
    };
    return std::apply(mk, std::move(x.values_));
  }

 private:
  N* out_;
  std::tuple<T...> values_;
};
}  // namespace detail

class BaseNode;

// Object used to hold live data for a channel. This data is exposed via the
// channelz service:
// https://github.com/grpc/proposal/blob/master/A14-channelz.md
class ChannelTrace {
  struct EntryRef {
    uint16_t id;
    uint16_t salt;
    static EntryRef Sentinel() { return EntryRef{kSentinelId, 0}; }
  };

 public:
  explicit ChannelTrace(size_t max_memory)
      : max_memory_(std::min(max_memory, sizeof(Entry) * 32768)) {}

  using Renderer = detail::Renderer;

  // Represents a node in the channel trace.
  // Nodes form a tree structure, allowing for hierarchical tracing.
  //
  // A `Node` is created by calling `ChannelTrace::NewNode()` for a root-level
  // event, or `Node::NewChild()` to create a child of an existing node.
  //
  // The `Node` object acts as a handle to an entry in the `ChannelTrace`.
  // By default, a `Node` is temporary. If the `Node` object is destroyed
  // (e.g., goes out of scope) without `Commit()` being called, the
  // corresponding trace entry is removed from the `ChannelTrace`. This RAII
  // behavior is useful for tracing events that might be cancelled or
  // superseded.
  //
  // To make a trace entry permanent (until it's evicted by memory limits),
  // call `Commit()` on the `Node` object. After `Commit()` is called, the
  // `Node` object can be destroyed without affecting the trace entry.
  //
  // `Node` objects are move-only to ensure clear ownership of the trace entry
  // handle.
  //
  // Example:
  //   ChannelTrace tracer(max_memory);
  //   // Create a root node
  //   auto root_node = tracer.NewNode("Root event");
  //   // Create a child node
  //   auto child_node = root_node.NewChild("Child event: ", 123);
  //   // If something goes wrong before committing:
  //   if (error) {
  //     // child_node and root_node go out of scope, entries are removed
  //     return;
  //   }
  //   // Commit the nodes to make them permanent
  //   child_node.Commit();
  //   root_node.Commit();
  class Node final {
   public:
    // Default constructor creates an invalid/sentinel Node.
    // Operations on a default-constructed Node are no-ops or return
    // invalid/sentinel results.
    Node() : trace_(nullptr), ref_(EntryRef::Sentinel()) {}
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&& other) noexcept
        : trace_(std::exchange(other.trace_, nullptr)),
          ref_(other.ref_),
          committed_(other.committed_) {}
    Node& operator=(Node&& other) noexcept {
      std::swap(trace_, other.trace_);
      std::swap(ref_, other.ref_);
      std::swap(committed_, other.committed_);
      return *this;
    }
    // If the `Node` was not committed, its corresponding entry is removed
    // from the `ChannelTrace`.
    ~Node() {
      if (trace_ != nullptr && !committed_ && ref_.id != kSentinelId) {
        trace_->DropEntry(ref_);
      }
    }

    // Creates a new child node associated with this node.
    // The child node will use the provided `renderer` to generate its
    // description.
    // Returns a new `Node` object representing the child.
    // If this node is invalid (e.g., default-constructed or moved-from),
    // an invalid `Node` is returned.
    [[nodiscard]] Node NewNode(std::unique_ptr<Renderer> renderer) {
      if (trace_ == nullptr || ref_.id == kSentinelId) return Node();
      return Node(trace_, trace_->AppendEntry(ref_, std::move(renderer)));
    }

    // Creates a new child node associated with this node.
    // The child node's description is formed by concatenating `args...`.
    // Supported types for `args` are those compatible with `absl::StrCat`
    // and `absl::string_view`.
    // Returns a new `Node` object representing the child.
    // If this node is invalid (e.g., default-constructed or moved-from),
    // an invalid `Node` is returned.
    template <typename... Args>
    [[nodiscard]] Node NewNode(Args&&... args) {
      if (trace_ == nullptr || ref_.id == kSentinelId) return Node();
      return NewNode(
          detail::RendererFromConcatenation(std::forward<Args>(args)...));
    }

    // Marks the trace entry associated with this `Node` as permanent.
    // After `Commit()`, destroying this `Node` object will no longer remove
    // the entry from the `ChannelTrace`.
    // If the node is invalid, this is a no-op.
    void Commit() {
      if (trace_ == nullptr || ref_.id == kSentinelId) return;
      committed_ = true;
    }

    bool ProducesOutput() const { return ref_.id != kSentinelId; }

   private:
    friend class ChannelTrace;

    Node(ChannelTrace* trace, EntryRef ref) : trace_(trace), ref_(ref) {}

    ChannelTrace* trace_;
    EntryRef ref_;
    bool committed_ = false;
  };

  [[nodiscard]] Node NewNode(std::unique_ptr<Renderer> render) {
    return Node(this, AppendEntry(EntryRef::Sentinel(), std::move(render)));
  }

  template <typename... Args>
  [[nodiscard]] Node NewNode(Args&&... args) {
    return NewNode(
        detail::RendererFromConcatenation(std::forward<Args>(args)...));
  }

  // Creates and returns the raw Json object, so a parent channelz
  // object may incorporate the json before rendering.
  Json RenderJson() const;

  void ForEachTraceEvent(
      absl::FunctionRef<void(gpr_timespec, std::string)> callback) const
      ABSL_LOCKS_EXCLUDED(mu_);

  void Render(grpc_channelz_v2_Entity* entity, upb_Arena* arena) const;

  bool ProducesOutput() const { return max_memory_ > 0; }

  std::string creation_timestamp() const;
  uint64_t num_events_logged() const {
    MutexLock lock(&mu_);
    return num_events_logged_;
  }

 private:
  friend size_t testing::GetSizeofTraceEvent(void);

  void ForEachTraceEventLocked(
      absl::FunctionRef<void(gpr_timespec, std::string)> callback) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  static constexpr uint16_t kSentinelId = 65535;

  // Internal representation of a trace entry.
  // These entries are stored in a std::vector `entries_` within ChannelTrace.
  // They form a tree structure (parent/child/sibling links) and also a
  // doubly-linked chronological list.
  //
  // The size of this struct is critical for memory management.
  // `ChannelTrace` uses `sizeof(Entry)` to estimate memory usage and enforce
  // `max_memory_`. Avoid adding fields or changing types that would
  // significantly increase its size. The `uint16_t` types for IDs are used
  // to keep the struct compact, limiting the total number of active (including
  // free-list) entries to 65535.
  struct Entry {
    Timestamp when;  // Timestamp of the event.
    // A counter incremented each time an entry at a particular index in
    // `entries_` is reused. Used by `EntryRef` to validate if a
    // reference is still pointing to the same logical entry.
    uint16_t salt = 0;
    // Index of the parent entry in `entries_`, or `kSentinelId`.
    uint16_t parent;
    // Index of the first child of this entry, or `kSentinelId`.
    uint16_t first_child;
    // Index of the last child of this entry, or `kSentinelId`.
    uint16_t last_child;
    // Index of the previous sibling, or `kSentinelId`.
    uint16_t prev_sibling;
    // Index of the next sibling, or `kSentinelId`.
    uint16_t next_sibling;
    // Index of the previous entry in chronological order, or `kSentinelId`.
    uint16_t prev_chronologically;
    // Index of the next entry in chronological order, or `kSentinelId`.
    uint16_t next_chronologically;
    // Pointer to a Renderer object that can generate the string
    // description for this trace event.
    std::unique_ptr<Renderer> renderer;

    // The basic MemoryUsage function doesn't work reliably cross platform for
    // std::unique_ptr within a struct. Open-code that part here.
    size_t MemoryUsage() const {
      if (renderer == nullptr) return sizeof(*this);
      return MemoryUsageOf(*renderer) + sizeof(*this);
    }
  };

  EntryRef AppendEntry(EntryRef parent, std::unique_ptr<Renderer> renderer)
      ABSL_LOCKS_EXCLUDED(mu_);
  EntryRef NewEntry(EntryRef parent, std::unique_ptr<Renderer> renderer)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void DropEntry(EntryRef entry) ABSL_LOCKS_EXCLUDED(mu_);
  void DropEntryId(uint16_t id) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void RenderEntry(const Entry& entry,
                   absl::FunctionRef<void(gpr_timespec, std::string)> callback,
                   int depth) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void RenderEntry(const Entry& entry, grpc_channelz_v2_TraceEvent* trace_event,
                   upb_Arena* arena) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  mutable Mutex mu_;
  const Timestamp time_created_ = Timestamp::Now();
  uint64_t num_events_logged_ ABSL_GUARDED_BY(mu_) = 0;
  const uint32_t max_memory_;
  uint32_t current_memory_ ABSL_GUARDED_BY(mu_) = 0;
  uint16_t next_free_entry_ ABSL_GUARDED_BY(mu_) = kSentinelId;
  uint16_t first_entry_ ABSL_GUARDED_BY(mu_) = kSentinelId;
  uint16_t last_entry_ ABSL_GUARDED_BY(mu_) = kSentinelId;
  std::vector<Entry> entries_ ABSL_GUARDED_BY(mu_);
};

// A node that GRPC_CHANNELZ_TRACE can output to, that also
// logs to absl LOG(INFO) if a particular TraceFlag is enabled.
// Provides a way to elevate GRPC_TRACE_LOG statements to channelz
// also.
class TraceNode {
 public:
  TraceNode() = default;

  template <typename F>
  TraceNode(ChannelTrace::Node node, TraceFlag& flag, F prefix)
      : node_(std::move(node)) {
    if (GPR_UNLIKELY(flag.enabled())) {
      log_to_absl_ = std::make_unique<std::string>(prefix());
    }
  }

  bool ProducesOutput() const {
    return node_.ProducesOutput() || log_to_absl_ != nullptr;
  }

  void Finish(std::unique_ptr<detail::Renderer> renderer) {
    if (GPR_UNLIKELY(log_to_absl_ != nullptr)) {
      LOG(INFO) << *log_to_absl_ << renderer->Render();
    }
    node_.NewNode(std::move(renderer)).Commit();
  }

  void Commit() { node_.Commit(); }

 private:
  ChannelTrace::Node node_;
  std::unique_ptr<std::string> log_to_absl_;
};

namespace detail {
inline ChannelTrace* LogOutputFrom(ChannelTrace& t) {
  if (!t.ProducesOutput()) return nullptr;
  return &t;
}

inline ChannelTrace::Node* LogOutputFrom(ChannelTrace::Node& n) {
  if (!n.ProducesOutput()) return nullptr;
  return &n;
}

inline TraceNode* LogOutputFrom(TraceNode& n) {
  if (!n.ProducesOutput()) return nullptr;
  return &n;
}

inline void OutputLogFromLogExpr(TraceNode* out,
                                 std::unique_ptr<Renderer> renderer) {
  out->Finish(std::move(renderer));
}
}  // namespace detail

}  // namespace channelz
}  // namespace grpc_core

// Log like LOG() to a channelz object (and potentially as part of a GRPC_TRACE
// log with channelz::TraceNode).
//
// `output` is one of a ChannelTrace, ChannelTrace::Node or a TraceNode.
//
// This trace always commits - and the channelz node is inaccessible as a
// result. Use it for annotation like things, and if commit-ability is
// important, put it under a parent node and use that for `output`.
//
// Notes on this weird macro!
// - We want this to be a statement level thing, such that end of statement ==>
//   we can commit the log line.
// - To do that we need to ensure we're not an expression, so we want an if,
//   for, while, or do statement enclosing things.
// - But hey! we want to stream after the GRPC_CHANNELZ_LOG(foo), so we want an
//   if right - if (we_can_output) output << s1 << s2 << s3;
// - But hey! now if someone copy/pastes this in front of an else then we
//   bind with that else, and boom we've got a security hole... so let's not do
//   that.
// - So, the for here acts as an if (the output = nullptr part ensures we don't)
//   actually loop), ensures we have a statement, and ensures we don't
//   accidentally bind with a trailing else.
// - And of course we skip wrapping things in {} because we really like that
//   GRPC_CHANNELZ_LOG(foo) << "hello!"; syntax.
#define GRPC_CHANNELZ_LOG(output)                                      \
  for (auto* out = grpc_core::channelz::detail::LogOutputFrom(output); \
       out != nullptr; out = nullptr)                                  \
  grpc_core::channelz::detail::LogExpr<                                \
      std::remove_reference_t<decltype(*out)>>(out)

#endif  // GRPC_SRC_CORE_CHANNELZ_CHANNEL_TRACE_H
