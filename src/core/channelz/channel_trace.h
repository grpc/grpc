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
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <variant>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/json/json.h"
#include "src/core/util/memory_usage.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/single_set_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace channelz {

namespace testing {
size_t GetSizeofTraceEvent(void);
}

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

  class Renderer {
   public:
    virtual ~Renderer() = default;
    virtual std::string Render() const = 0;
    virtual size_t MemoryUsage() const = 0;
  };

  enum Severity {
    Unset = 0,  // never to be used
    Info,       // we start at 1 to avoid using proto default values
    Warning,
    Error
  };

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
    // Moves the trace entry handle from `other` to this `Node`.
    // `other` becomes invalid (its trace_ pointer is nullified).
    Node(Node&& other) noexcept
        : trace_(std::exchange(other.trace_, nullptr)),
          ref_(std::exchange(other.ref_, EntryRef::Sentinel())),
          committed_(std::exchange(other.committed_, false)) {}
    // Moves the trace entry handle from `other` to this `Node`.
    // `other` becomes invalid. If this `Node` previously held an uncommitted
    // trace entry, that entry is dropped.
    Node& operator=(Node&& other) noexcept {
      if (this == &other) return *this;
      // If `this` node was managing an uncommitted entry, drop it first.
      if (trace_ != nullptr && !committed_ && ref_.id != kSentinelId) {
        trace_->DropEntry(ref_);
      }
      trace_ = std::exchange(other.trace_, nullptr);
      ref_ = std::exchange(other.ref_, EntryRef::Sentinel());
      committed_ = std::exchange(other.committed_, false);
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
    [[nodiscard]] Node NewChild(std::unique_ptr<Renderer> renderer) {
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
    [[nodiscard]] Node NewChild(Args&&... args) {
      if (trace_ == nullptr || ref_.id == kSentinelId) return Node();
      return NewChild(RendererFromConcatenation(std::forward<Args>(args)...));
    }

    // Marks the trace entry associated with this `Node` as permanent.
    // After `Commit()`, destroying this `Node` object will no longer remove
    // the entry from the `ChannelTrace`.
    // If the node is invalid, this is a no-op.
    void Commit() {
      if (trace_ == nullptr || ref_.id == kSentinelId) return;
      committed_ = true;
    }

   private:
    friend class ChannelTrace;

    Node(ChannelTrace* trace, EntryRef ref) : trace_(trace), ref_(ref) {}

    ChannelTrace* trace_;
    EntryRef ref_;
    bool committed_ = false;
  };

  static const char* SeverityString(ChannelTrace::Severity severity) {
    switch (severity) {
      case ChannelTrace::Severity::Info:
        return "CT_INFO";
      case ChannelTrace::Severity::Warning:
        return "CT_WARNING";
      case ChannelTrace::Severity::Error:
        return "CT_ERROR";
      default:
        GPR_UNREACHABLE_CODE(return "CT_UNKNOWN");
    }
  }

  [[nodiscard]] Node NewNode(std::unique_ptr<Renderer> render) {
    return Node(this, AppendEntry(EntryRef::Sentinel(), std::move(render)));
  }

  template <typename... Args>
  [[nodiscard]] Node NewNode(Args&&... args) {
    return NewNode(RendererFromConcatenation(std::forward<Args>(args)...));
  }

  // Creates and returns the raw Json object, so a parent channelz
  // object may incorporate the json before rendering.
  Json RenderJson() const;

  void ForEachTraceEvent(
      absl::FunctionRef<void(gpr_timespec, Severity, std::string,
                             RefCountedPtr<BaseNode>)>
          callback) const ABSL_LOCKS_EXCLUDED(mu_);

 private:
  friend size_t testing::GetSizeofTraceEvent(void);

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
  };

  EntryRef AppendEntry(EntryRef parent, std::unique_ptr<Renderer> renderer)
      ABSL_LOCKS_EXCLUDED(mu_);
  EntryRef NewEntry(EntryRef parent, std::unique_ptr<Renderer> renderer)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void DropEntry(EntryRef entry) ABSL_LOCKS_EXCLUDED(mu_);
  void DropEntryId(uint16_t id) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  template <typename T>
  static size_t MemoryUsageOf(const T& x) {
    return MemoryUsage(x);
  }

  struct StrCatFn {
    template <typename... Arg>
    std::string operator()(const Arg&... args) {
      return absl::StrCat(args...);
    }
  };

  template <typename A>
  static auto AdaptForStorage(A&& a) {
    using RawA = std::remove_reference_t<A>;
    if constexpr (std::is_same_v<std::decay_t<RawA>, const char*>) {
      return absl::string_view(a);
    } else {
      return RawA(a);
    }
  }

  template <typename... Args>
  static std::unique_ptr<Renderer> RendererFromConcatenationInner(
      Args&&... args) {
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
  static std::unique_ptr<Renderer> RendererFromConcatenation(Args&&... args) {
    return RendererFromConcatenationInner(
        AdaptForStorage<Args>(std::forward<Args>(args))...);
  }

  void RenderEntry(const Entry& entry,
                   absl::FunctionRef<void(gpr_timespec, Severity, std::string,
                                          RefCountedPtr<BaseNode>)>
                       callback,
                   int depth) const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  mutable Mutex mu_;
  const uint32_t max_memory_;
  uint32_t current_memory_ ABSL_GUARDED_BY(mu_) = 0;
  uint16_t next_free_entry_ ABSL_GUARDED_BY(mu_) = kSentinelId;
  uint16_t first_entry_ ABSL_GUARDED_BY(mu_) = kSentinelId;
  uint16_t last_entry_ ABSL_GUARDED_BY(mu_) = kSentinelId;
  std::vector<Entry> entries_ ABSL_GUARDED_BY(mu_);
};

}  // namespace channelz
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CHANNELZ_CHANNEL_TRACE_H
