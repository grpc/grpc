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

#include <limits>
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
 public:
  explicit ChannelTrace(size_t max_nodes) : max_nodes_(max_nodes) {}

  class Renderer {
   public:
    virtual ~Renderer();
    virtual std::string Render() const = 0;
    virtual size_t MemoryUsage() const = 0;
  };

  enum Severity {
    Unset = 0,  // never to be used
    Info,       // we start at 1 to avoid using proto default values
    Warning,
    Error
  };

  class Node final : public DualRefCounted<Node, NonPolymorphicRefCount> {
   public:
    void Update(std::unique_ptr<Renderer> renderer) {
      MutexLock lock(&mu_);
      renderer.swap(renderer_);
    }

    RefCountedPtr<Node> NewChild(
        std::unique_ptr<Renderer> renderer,
        size_t max_nodes = std::numeric_limits<size_t>::max()) {
      auto when = gpr_now(GPR_CLOCK_REALTIME);
      auto node = RefCountedPtr<Node>(
          new Node(WeakRef(), std::move(renderer), max_nodes));
      Entry removed_entry;
      MutexLock lock(&mu_);
      if (children_.size() == max_nodes_) {
        removed_entry = std::move(*children_.begin());
        children_.erase(children_.begin());
      }
      children_.push_back({when, node->WeakRef()});
      return node;
    }

    template <typename... Args>
    RefCountedPtr<Node> NewChild(Args&&... args) {
      return NewChild(RendererFromConcatenation(std::forward<Args>(args)...));
    }

    // Commit this line to the parent trace forever (or until that trace is
    // expunged).
    // Uncommitted nodes will be erased from the trace upon orphaning.
    void Commit() {
      CHECK(parent_ != nullptr);
      MutexLock lock(&parent_->mu_);
      for (auto& child : parent_->children_) {
        if (auto* p = std::get_if<WeakRefCountedPtr<Node>>(&child.value);
            p != nullptr) {
          child.value = Ref();
          return;
        }
      }
    }

    void Orphaned() override {
      if (parent_ == nullptr) return;
      parent_->mu_.Lock();
      parent_->children_.erase(
          std::remove_if(
              parent_->children_.begin(), parent_->children_.end(),
              [this](const Entry& entry) {
                if (std::holds_alternative<RefCountedPtr<Node>>(entry.value)) {
                  return false;
                }
                return std::get<WeakRefCountedPtr<Node>>(entry.value).get() ==
                       this;
              }),
          parent_->children_.end());
      parent_->mu_.Unlock();
      parent_.reset();
    }

    void ForEachTraceEvent(
        absl::FunctionRef<void(gpr_timespec, int, absl::string_view)> output,
        int indent) const {
      mu_.Lock();
      std::vector<Entry> children = children_;
      mu_.Unlock();
      for (auto& child : children) {
        WeakRefCountedPtr<Node> n = child.weak_value();
        if (n == nullptr) continue;
        n->mu_.Lock();
        if (n->renderer_ == nullptr) {
          n->mu_.Unlock();
        } else {
          std::string text = n->renderer_->Render();
          n->mu_.Unlock();
          output(child.timestamp, indent, text);
        }
        n->ForEachTraceEvent(output, indent + 1);
      }
    }

   private:
    struct Entry {
      gpr_timespec timestamp;
      std::variant<RefCountedPtr<Node>, WeakRefCountedPtr<Node>> value;

      WeakRefCountedPtr<Node> weak_value() {
        if (auto* p = std::get_if<RefCountedPtr<Node>>(&value); p != nullptr) {
          return (*p)->WeakRef();
        }
        return std::get<WeakRefCountedPtr<Node>>(value);
      }
    };

    Node(WeakRefCountedPtr<Node> parent, std::unique_ptr<Renderer> renderer,
         size_t max_nodes)
        : parent_(std::move(parent)),
          max_nodes_(max_nodes),
          renderer_(std::move(renderer)) {}

    friend class ChannelTrace;

    WeakRefCountedPtr<Node> parent_;
    mutable Mutex mu_;
    const size_t max_nodes_;
    std::vector<Entry> children_ ABSL_GUARDED_BY(mu_);
    std::unique_ptr<Renderer> renderer_ ABSL_GUARDED_BY(mu_);
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

  RefCountedPtr<Node> NewNode(std::unique_ptr<Renderer> render) {
    if (max_nodes_ == 0) {
      return RefCountedPtr<Node>(new Node(nullptr, std::move(render), 0));
    }
    return root_
        .GetOrCreate([this]() {
          return RefCountedPtr<Node>(new Node(nullptr, nullptr, max_nodes_));
        })
        ->NewChild(std::move(render));
  }

  template <typename... Args>
  RefCountedPtr<Node> NewNode(Args&&... args) {
    return NewNode(RendererFromConcatenation(std::forward<Args>(args)...));
  }

  // Creates and returns the raw Json object, so a parent channelz
  // object may incorporate the json before rendering.
  Json RenderJson() const;

  void ForEachTraceEvent(
      absl::FunctionRef<void(gpr_timespec, Severity, std::string,
                             RefCountedPtr<BaseNode>)>
          callback) const {
    auto* node = root_.Get();
    if (node == nullptr) return;
    node->ForEachTraceEvent(
        [callback](gpr_timespec timestamp, int indent, absl::string_view line) {
          CHECK_GE(indent, 0);
          callback(timestamp, Severity::Info,
                   absl::StrCat(std::string(' ', indent), line), nullptr);
        },
        -1);
  }

 private:
  friend size_t testing::GetSizeofTraceEvent(void);

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
      return std::forward<A>(a);
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

  const size_t max_nodes_;
  SingleSetRefCountedPtr<Node> root_;
};

}  // namespace channelz
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CHANNELZ_CHANNEL_TRACE_H
