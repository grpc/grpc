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

#include "src/core/call/call_filters.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test/core/promise/poll_matcher.h"

using testing::Mock;
using testing::StrictMock;

namespace grpc_core {

namespace {
// A mock activity that can be activated and deactivated.
class MockActivity : public Activity, public Wakeable {
 public:
  MOCK_METHOD(void, WakeupRequested, ());

  void ForceImmediateRepoll(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Orphan() override {}
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask /*mask*/) override { WakeupRequested(); }
  void WakeupAsync(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Drop(WakeupMask /*mask*/) override {}
  std::string DebugTag() const override { return "MockActivity"; }
  std::string ActivityDebugTag(WakeupMask /*mask*/) const override {
    return DebugTag();
  }

  void Activate() {
    if (scoped_activity_ == nullptr) {
      scoped_activity_ = std::make_unique<ScopedActivity>(this);
    }
  }

  void Deactivate() { scoped_activity_.reset(); }

 private:
  std::unique_ptr<ScopedActivity> scoped_activity_;
};

#define EXPECT_WAKEUP(activity, statement)                                 \
  EXPECT_CALL((activity), WakeupRequested()).Times(::testing::AtLeast(1)); \
  statement;                                                               \
  Mock::VerifyAndClearExpectations(&(activity));

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Layout

namespace filters_detail {

TEST(LayoutTest, Empty) {
  Layout<ClientMetadataHandle> l;
  ASSERT_EQ(l.ops.size(), 0u);
  EXPECT_EQ(l.promise_size, 0u);
  EXPECT_EQ(l.promise_alignment, 0u);
}

TEST(LayoutTest, Add) {
  Layout<ClientMetadataHandle> l;
  l.Add(1, 4,
        Operator<ClientMetadataHandle>{&l, 120, nullptr, nullptr, nullptr});
  ASSERT_EQ(l.ops.size(), 1u);
  EXPECT_EQ(l.promise_size, 1u);
  EXPECT_EQ(l.promise_alignment, 4u);
  EXPECT_EQ(l.ops[0].call_offset, 120);
}

}  // namespace filters_detail

////////////////////////////////////////////////////////////////////////////////
// StackData

namespace filters_detail {

TEST(StackDataTest, Empty) {
  StackData d;
  EXPECT_EQ(d.call_data_alignment, 1u);
  EXPECT_EQ(d.call_data_size, 0u);
}

TEST(StackDataTest, OneByteAlignmentAndSize) {
  struct Filter1 {
    struct Call {
      char c;
    };
  };
  static_assert(alignof(typename Filter1::Call) == 1,
                "Expect 1 byte alignment");
  static_assert(sizeof(typename Filter1::Call) == 1, "Expect 1 byte size");
  StackData d;
  Filter1 f1;
  d.AddFilter(&f1);
  EXPECT_EQ(d.call_data_alignment, 1);
  EXPECT_EQ(d.call_data_size, 1);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  // Check channel data
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  // Check call offsets
  EXPECT_EQ(d.filter_constructor[0].call_offset, 0);
}

TEST(StackDataTest, PointerAlignmentAndSize) {
  struct Filter1 {
    struct Call {
      void* p;
    };
  };
  static_assert(alignof(typename Filter1::Call) == alignof(void*),
                "Expect pointer alignment");
  static_assert(sizeof(typename Filter1::Call) == sizeof(void*),
                "Expect pointer size");
  StackData d;
  Filter1 f1;
  d.AddFilter(&f1);
  EXPECT_EQ(d.call_data_alignment, alignof(void*));
  EXPECT_EQ(d.call_data_size, sizeof(void*));
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  // Check channel data
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  // Check call offsets
  EXPECT_EQ(d.filter_constructor[0].call_offset, 0);
}

TEST(StackDataTest, PointerAndOneByteAlignmentAndSize) {
  struct Filter1 {
    struct Call {
      char c;
    };
  };
  static_assert(alignof(typename Filter1::Call) == 1,
                "Expect 1 byte alignment");
  static_assert(sizeof(typename Filter1::Call) == 1, "Expect 1 byte size");
  struct Filter2 {
    struct Call {
      void* p;
    };
  };
  static_assert(alignof(typename Filter2::Call) == alignof(void*),
                "Expect pointer alignment");
  static_assert(sizeof(typename Filter2::Call) == sizeof(void*),
                "Expect pointer size");
  StackData d;
  Filter1 f1;
  Filter2 f2;
  d.AddFilter(&f1);
  d.AddFilter(&f2);
  EXPECT_EQ(d.call_data_alignment, alignof(void*));
  // Padding added after 1-byte element to align pointer.
  EXPECT_EQ(d.call_data_size, 2 * sizeof(void*));
  ASSERT_EQ(d.filter_constructor.size(), 2u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  // Check channel data
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[1].channel_data, &f2);
  // Check call offsets
  EXPECT_EQ(d.filter_constructor[0].call_offset, 0);
  EXPECT_EQ(d.filter_constructor[1].call_offset, sizeof(void*));
}

TEST(StackDataTest, PointerAndOneByteAlignmentAndSizeBackwards) {
  struct Filter1 {
    struct Call {
      char c;
    };
  };
  static_assert(alignof(typename Filter1::Call) == 1,
                "Expect 1 byte alignment");
  static_assert(sizeof(typename Filter1::Call) == 1, "Expect 1 byte size");
  struct Filter2 {
    struct Call {
      void* p;
    };
  };
  static_assert(alignof(typename Filter2::Call) == alignof(void*),
                "Expect pointer alignment");
  static_assert(sizeof(typename Filter2::Call) == sizeof(void*),
                "Expect pointer size");
  StackData d;
  Filter1 f1;
  Filter2 f2;
  d.AddFilter(&f2);
  d.AddFilter(&f1);
  EXPECT_EQ(d.call_data_alignment, alignof(void*));
  // No padding needed, so just the sum of sizes.
  EXPECT_EQ(d.call_data_size, sizeof(void*) + 1);
  ASSERT_EQ(d.filter_constructor.size(), 2u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  // Check channel data
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f2);
  EXPECT_EQ(d.filter_constructor[1].channel_data, &f1);
  // Check call offsets
  EXPECT_EQ(d.filter_constructor[0].call_offset, 0);
  EXPECT_EQ(d.filter_constructor[1].call_offset, sizeof(void*));
}

TEST(StackDataTest, EmptyFilter) {
  struct Filter1 {
    struct Call {};
  };
  static_assert(std::is_empty<typename Filter1::Call>::value,
                "Expect empty filter");
  StackData d;
  Filter1 f1;
  d.AddFilter(&f1);
  EXPECT_EQ(d.call_data_size, 0);
}

TEST(StackDataTest, OneFilterThenManyEmptyThenOneNonEmpty) {
  struct Filter1 {
    struct Call {
      char c;
    };
  };
  struct Filter2 {
    struct Call {};
  };
  StackData d;
  // Declare filters
  Filter1 f1a;
  Filter2 f2a;
  Filter2 f2b;
  Filter2 f2c;
  Filter2 f2d;
  Filter1 f1b;
  // Add filters
  d.AddFilter(&f1a);
  d.AddFilter(&f2a);
  d.AddFilter(&f2b);
  d.AddFilter(&f2c);
  d.AddFilter(&f2d);
  d.AddFilter(&f1b);
  // Check overall size
  EXPECT_EQ(d.call_data_size, 2);
  ASSERT_EQ(d.filter_constructor.size(), 2u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  // Check channel data
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1a);
  EXPECT_EQ(d.filter_constructor[1].channel_data, &f1b);
  // Check call offsets
  EXPECT_EQ(d.filter_constructor[0].call_offset, 0);
  EXPECT_EQ(d.filter_constructor[1].call_offset, 1);
}

TEST(StackDataTest, FilterInit) {
  struct Filter1 {
    struct Call {
      std::unique_ptr<int> p{new int(42)};
    };
  };
  StackData d;
  Filter1 f1;
  d.AddFilter(&f1);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 1u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, 0);
  EXPECT_EQ(d.filter_destructor[0].call_offset, 0);
  void* p = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(p, &f1);
  EXPECT_EQ(*static_cast<Filter1::Call*>(p)->p, 42);
  d.filter_destructor[0].call_destroy(p);
  gpr_free_aligned(p);
}

TEST(StackDataTest, FilterInitWithArg) {
  struct Filter1 {
    struct Call {
      explicit Call(Filter1* f) : p(new Filter1*(f)) {}
      std::unique_ptr<Filter1*> p;
    };
  };
  StackData d;
  Filter1 f1;
  d.AddFilter(&f1);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 1u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, 0);
  EXPECT_EQ(d.filter_destructor[0].call_offset, 0);
  void* p = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(p, &f1);
  EXPECT_EQ(*static_cast<Filter1::Call*>(p)->p, &f1);
  d.filter_destructor[0].call_destroy(p);
  gpr_free_aligned(p);
}

TEST(StackDataTest, InstantClientInitialMetadataReturningVoid) {
  struct Filter1 {
    struct Call {
      void OnClientInitialMetadata(ClientMetadata& md) {
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
      }
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  EXPECT_EQ(d.call_data_size, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 0u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  char call_data;
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, &call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
}

TEST(StackDataTest, InstantClientInitialMetadataReturningVoidTakingChannelPtr) {
  struct Filter1 {
    struct Call {
      void OnClientInitialMetadata(ClientMetadata& md, Filter1* p) {
        p->v.push_back(42);
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
      }
    };
    std::vector<int> v;
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  EXPECT_EQ(d.call_data_size, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 0u);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  char call_data;
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, &call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  EXPECT_THAT(f1.v, ::testing::ElementsAre(42));
}

TEST(StackDataTest, InstantClientInitialMetadataReturningAbslStatus) {
  struct Filter1 {
    class Call {
     public:
      absl::Status OnClientInitialMetadata(ClientMetadata& md) {
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
        bool first = std::exchange(first_, false);
        return first ? absl::OkStatus() : absl::CancelledError();
      }

     private:
      bool first_ = true;
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, call_offset);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(call_data, &f1);
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  promise_detail::Context<Arena> ctx(arena.get());
  // A succeeding call
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  // A failing call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  gpr_free_aligned(call_data);
}

TEST(StackDataTest,
     InstantClientInitialMetadataReturningAbslStatusTakingChannelPtr) {
  struct Filter1 {
    class Call {
     public:
      absl::Status OnClientInitialMetadata(ClientMetadata& md, Filter1* p) {
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
        const bool first = std::exchange(first_, false);
        p->v.push_back(first ? 11 : 22);
        return first ? absl::OkStatus() : absl::CancelledError();
      }

     private:
      bool first_ = true;
    };
    std::vector<int> v;
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, call_offset);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(call_data, &f1);
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  promise_detail::Context<Arena> ctx(arena.get());
  // A succeeding call
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  // A failing call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  gpr_free_aligned(call_data);
  EXPECT_THAT(f1.v, ::testing::ElementsAre(11, 22));
}

TEST(StackDataTest, InstantClientInitialMetadataReturningServerMetadata) {
  struct Filter1 {
    class Call {
     public:
      ServerMetadataHandle OnClientInitialMetadata(ClientMetadata& md) {
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
        bool first = std::exchange(first_, false);
        return first ? nullptr
                     : ServerMetadataFromStatus(GRPC_STATUS_CANCELLED);
      }

     private:
      bool first_ = true;
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, call_offset);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(call_data, &f1);
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  promise_detail::Context<Arena> ctx(arena.get());
  // A succeeding call
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  // A failing call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  gpr_free_aligned(call_data);
}

TEST(StackDataTest,
     InstantClientInitialMetadataReturningServerMetadataTakingChannelPtr) {
  struct Filter1 {
    class Call {
     public:
      ServerMetadataHandle OnClientInitialMetadata(ClientMetadata& md,
                                                   Filter1* p) {
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
        const bool first = std::exchange(first_, false);
        p->v.push_back(first ? 11 : 22);
        return first ? nullptr
                     : ServerMetadataFromStatus(GRPC_STATUS_CANCELLED);
      }

     private:
      bool first_ = true;
    };
    std::vector<int> v;
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, call_offset);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(call_data, &f1);
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  promise_detail::Context<Arena> ctx(arena.get());
  // A succeeding call
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  // A failing call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  gpr_free_aligned(call_data);
  EXPECT_THAT(f1.v, ::testing::ElementsAre(11, 22));
}

TEST(StackDataTest, PromiseClientInitialMetadataReturningAbslStatus) {
  struct Filter1 {
    class Call {
     public:
      auto OnClientInitialMetadata(ClientMetadata& md) {
        return [this, i = std::make_unique<int>(3),
                md = &md]() mutable -> Poll<absl::Status> {
          --*i;
          if (*i > 0) return Pending{};
          md->Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
          bool first = std::exchange(first_, false);
          return first ? absl::OkStatus() : absl::CancelledError();
        };
      }

     private:
      bool first_ = true;
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, call_offset);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(call_data, &f1);
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  promise_detail::Context<Arena> ctx(arena.get());
  // A succeeding call
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  void* promise_data =
      gpr_malloc_aligned(d.client_initial_metadata.promise_size,
                         d.client_initial_metadata.promise_alignment);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  // A failing call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  // A cancelled call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  d.client_initial_metadata.ops[0].early_destroy(promise_data);
  // ASAN will trigger if things aren't cleaned up
  gpr_free_aligned(promise_data);
  gpr_free_aligned(call_data);
}

TEST(StackDataTest,
     PromiseClientInitialMetadataReturningAbslStatusTakingChannelPtr) {
  struct Filter1 {
    class Call {
     public:
      auto OnClientInitialMetadata(ClientMetadata& md, Filter1* p) {
        return [this, p, i = std::make_unique<int>(3),
                md = &md]() mutable -> Poll<absl::Status> {
          --*i;
          if (*i > 0) return Pending{};
          md->Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
          bool first = std::exchange(first_, false);
          p->v.push_back(first ? 11 : 22);
          return first ? absl::OkStatus() : absl::CancelledError();
        };
      }

     private:
      bool first_ = true;
    };
    std::vector<int> v;
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  d.AddClientInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 1u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  EXPECT_EQ(d.filter_constructor[0].channel_data, &f1);
  EXPECT_EQ(d.filter_constructor[0].call_offset, call_offset);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filter_constructor[0].call_init(call_data, &f1);
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ClientMetadata>();
  promise_detail::Context<Arena> ctx(arena.get());
  // A succeeding call
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  void* promise_data =
      gpr_malloc_aligned(d.client_initial_metadata.promise_size,
                         d.client_initial_metadata.promise_alignment);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  // A failing call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_FALSE(r.ready());
  r = d.client_initial_metadata.ops[0].poll(promise_data);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  // A cancelled call
  md = Arena::MakePooledForOverwrite<ClientMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  d.client_initial_metadata.ops[0].early_destroy(promise_data);
  // ASAN will trigger if things aren't cleaned up
  gpr_free_aligned(promise_data);
  gpr_free_aligned(call_data);
  EXPECT_THAT(f1.v, ::testing::ElementsAre(11, 22));
}

TEST(StackDataTest, InstantServerInitialMetadataReturningVoid) {
  struct Filter1 {
    struct Call {
      void OnServerInitialMetadata(ServerMetadata& md) {
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
      }
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  EXPECT_EQ(d.call_data_size, 0);
  d.AddServerInitialMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 0);
  ASSERT_EQ(d.filter_destructor.size(), 0);
  ASSERT_EQ(d.server_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.server_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.server_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.server_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.server_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  char call_data;
  auto r = d.server_initial_metadata.ops[0].promise_init(
      nullptr, &call_data, d.server_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
}

TEST(StackDataTest, InstantClientToServerMessagesReturningVoid) {
  struct Filter1 {
    struct Call {
      void OnClientToServerMessage(Message& message) {
        message.mutable_flags() |= 1;
      }
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  EXPECT_EQ(d.call_data_size, 0);
  d.AddClientToServerMessageOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 0u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  ASSERT_EQ(d.client_to_server_messages.ops.size(), 1u);
  EXPECT_EQ(d.client_to_server_messages.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_to_server_messages.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_to_server_messages.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_to_server_messages.ops[0].early_destroy, nullptr);
  // Check promise init
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto message = Arena::MakePooled<Message>(SliceBuffer(), 0);
  char call_data;
  auto r = d.client_to_server_messages.ops[0].promise_init(
      nullptr, &call_data, d.client_to_server_messages.ops[0].channel_data,
      std::move(message));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->flags(), 1u);
}

TEST(StackDataTest, InstantServerToClientMessagesReturningVoid) {
  struct Filter1 {
    struct Call {
      void OnServerToClientMessage(Message& message) {
        message.mutable_flags() |= 1;
      }
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  EXPECT_EQ(d.call_data_size, 0);
  d.AddServerToClientMessageOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 0u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  ASSERT_EQ(d.server_to_client_messages.ops.size(), 1u);
  EXPECT_EQ(d.server_to_client_messages.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.server_to_client_messages.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.server_to_client_messages.ops[0].poll, nullptr);
  EXPECT_EQ(d.server_to_client_messages.ops[0].early_destroy, nullptr);
  // Check promise init
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto message = Arena::MakePooled<Message>(SliceBuffer(), 0);
  char call_data;
  auto r = d.server_to_client_messages.ops[0].promise_init(
      nullptr, &call_data, d.server_to_client_messages.ops[0].channel_data,
      std::move(message));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->flags(), 1u);
}

TEST(StackDataTest, ServerTrailingMetadataReturningVoid) {
  struct Filter1 {
    struct Call {
      void OnServerTrailingMetadata(ServerMetadata& md) {
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
      }
    };
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  EXPECT_EQ(d.call_data_size, 0);
  d.AddServerTrailingMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 0u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  ASSERT_EQ(d.server_trailing_metadata.size(), 1u);
  EXPECT_EQ(d.server_trailing_metadata[0].call_offset, call_offset);
  EXPECT_EQ(d.server_trailing_metadata[0].channel_data, &f1);
  // Check operation
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  char call_data;
  auto r = d.server_trailing_metadata[0].server_trailing_metadata(
      &call_data, d.server_trailing_metadata[0].channel_data, std::move(md));
  EXPECT_EQ(r->get_pointer(HttpPathMetadata())->as_string_view(), "hello");
}

TEST(StackDataTest, ServerTrailingMetadataReturningVoidTakingChannelPtr) {
  struct Filter1 {
    struct Call {
      void OnServerTrailingMetadata(ServerMetadata& md, Filter1* p) {
        p->v.push_back(42);
        md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
      }
    };
    std::vector<int> v;
  };
  StackData d;
  Filter1 f1;
  const size_t call_offset = d.AddFilter(&f1);
  EXPECT_EQ(call_offset, 0);
  EXPECT_EQ(d.call_data_size, 0);
  d.AddServerTrailingMetadataOp(&f1, call_offset);
  ASSERT_EQ(d.filter_constructor.size(), 0u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  ASSERT_EQ(d.server_trailing_metadata.size(), 1u);
  EXPECT_EQ(d.server_trailing_metadata[0].call_offset, call_offset);
  EXPECT_EQ(d.server_trailing_metadata[0].channel_data, &f1);
  // Check operation
  auto arena = SimpleArenaAllocator()->MakeArena();
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  char call_data;
  auto r = d.server_trailing_metadata[0].server_trailing_metadata(
      &call_data, d.server_trailing_metadata[0].channel_data, std::move(md));
  EXPECT_EQ(r->get_pointer(HttpPathMetadata())->as_string_view(), "hello");
  EXPECT_THAT(f1.v, ::testing::ElementsAre(42));
}

}  // namespace filters_detail

///////////////////////////////////////////////////////////////////////////////
// StackBuilder

class CallFilters::StackTestSpouse {
 public:
  static const filters_detail::StackData& StackDataFrom(const Stack& stack) {
    return stack.data_;
  }
};

TEST(StackBuilderTest, AddOnServerTrailingMetadata) {
  CallFilters::StackBuilder b;
  b.AddOnServerTrailingMetadata(
      [x = std::make_unique<int>(42)](ServerMetadata&) { EXPECT_EQ(*x, 42); });
  auto stack = b.Build();
  const auto& data = CallFilters::StackTestSpouse().StackDataFrom(*stack);
  ASSERT_EQ(data.server_trailing_metadata.size(), 1u);
  ASSERT_EQ(data.client_initial_metadata.ops.size(), 0u);
  ASSERT_EQ(data.client_to_server_messages.ops.size(), 0u);
  ASSERT_EQ(data.server_to_client_messages.ops.size(), 0u);
  ASSERT_EQ(data.server_initial_metadata.ops.size(), 0u);
  EXPECT_EQ(data.server_trailing_metadata[0].call_offset, 0);
  EXPECT_NE(data.server_trailing_metadata[0].channel_data, nullptr);
}

///////////////////////////////////////////////////////////////////////////////
// OperationExecutor

namespace filters_detail {

TEST(OperationExecutorTest, NoOp) {
  OperationExecutor<ClientMetadataHandle> pipe;
  EXPECT_FALSE(pipe.IsRunning());
}

TEST(OperationExecutorTest, InstantTwo) {
  class Filter1 {
   public:
    class Call {
     public:
      absl::Status OnClientInitialMetadata(ClientMetadata& md) {
        bool first = std::exchange(first_, false);
        if (!first) {
          EXPECT_EQ(md.get_pointer(HttpPathMetadata()), nullptr);
        }
        if (md.get_pointer(HttpPathMetadata()) != nullptr) {
          md.Set(HttpPathMetadata(), Slice::FromStaticString("world"));
        } else {
          md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
        }
        return first ? absl::OkStatus() : absl::CancelledError();
      }

     private:
      bool first_ = true;
    };
  };
  StackData d;
  Filter1 f1;
  Filter1 f2;
  const size_t call_offset1 = d.AddFilter(&f1);
  const size_t call_offset2 = d.AddFilter(&f2);
  d.AddClientInitialMetadataOp(&f1, call_offset1);
  d.AddClientInitialMetadataOp(&f2, call_offset2);
  ASSERT_EQ(d.filter_constructor.size(), 2u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 2u);
  void* call_data1 =
      gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  void* call_data2 = Offset(call_data1, d.filter_constructor[1].call_offset);
  d.filter_constructor[0].call_init(call_data1, &f1);
  d.filter_constructor[1].call_init(call_data2, &f2);
  OperationExecutor<ClientMetadataHandle> transformer;
  auto arena = SimpleArenaAllocator()->MakeArena();
  promise_detail::Context<Arena> ctx(arena.get());
  // First call succeeds
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r =
      transformer.Start(&d.client_initial_metadata, std::move(md), call_data1);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "world");
  // Second fails
  md = Arena::MakePooledForOverwrite<ServerMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = transformer.Start(&d.client_initial_metadata, std::move(md), call_data1);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  gpr_free_aligned(call_data1);
}

TEST(OperationExecutorTest, PromiseTwo) {
  class Filter1 {
   public:
    class Call {
     public:
      auto OnClientInitialMetadata(ClientMetadata& md) {
        return [md = &md, this,
                i = std::make_unique<int>(3)]() mutable -> Poll<absl::Status> {
          --*i;
          if (*i > 0) return Pending{};
          bool first = std::exchange(first_, false);
          if (!first) {
            EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
          }
          if (md->get_pointer(HttpPathMetadata()) != nullptr) {
            md->Set(HttpPathMetadata(), Slice::FromStaticString("world"));
          } else {
            md->Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
          }
          return first ? absl::OkStatus() : absl::CancelledError();
        };
      }

     private:
      bool first_ = true;
    };
  };
  StackData d;
  Filter1 f1;
  Filter1 f2;
  const size_t call_offset1 = d.AddFilter(&f1);
  const size_t call_offset2 = d.AddFilter(&f2);
  d.AddClientInitialMetadataOp(&f1, call_offset1);
  d.AddClientInitialMetadataOp(&f2, call_offset2);
  ASSERT_EQ(d.filter_constructor.size(), 2u);
  ASSERT_EQ(d.filter_destructor.size(), 0u);
  ASSERT_EQ(d.client_initial_metadata.ops.size(), 2u);
  void* call_data1 =
      gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  void* call_data2 = Offset(call_data1, d.filter_constructor[1].call_offset);
  d.filter_constructor[0].call_init(call_data1, &f1);
  d.filter_constructor[1].call_init(call_data2, &f2);
  OperationExecutor<ClientMetadataHandle> transformer;
  auto arena = SimpleArenaAllocator()->MakeArena();
  promise_detail::Context<Arena> ctx(arena.get());
  // First call succeeds after two sets of two step delays.
  auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r =
      transformer.Start(&d.client_initial_metadata, std::move(md), call_data1);
  EXPECT_FALSE(r.ready());
  r = transformer.Step(call_data1);
  EXPECT_FALSE(r.ready());
  r = transformer.Step(call_data1);
  EXPECT_FALSE(r.ready());
  r = transformer.Step(call_data1);
  EXPECT_FALSE(r.ready());
  r = transformer.Step(call_data1);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "world");
  // Second fails after one set of two step delays.
  md = Arena::MakePooledForOverwrite<ServerMetadata>();
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = transformer.Start(&d.client_initial_metadata, std::move(md), call_data1);
  EXPECT_FALSE(r.ready());
  r = transformer.Step(call_data1);
  EXPECT_FALSE(r.ready());
  r = transformer.Step(call_data1);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  gpr_free_aligned(call_data1);
}

}  // namespace filters_detail

///////////////////////////////////////////////////////////////////////////////
// CallFilters

TEST(CallFiltersTest, CanBuildStack) {
  struct Filter {
    struct Call {
      void OnClientInitialMetadata(ClientMetadata&) {}
      void OnServerInitialMetadata(ServerMetadata&) {}
      void OnClientToServerMessage(Message&) {}
      void OnClientToServerHalfClose() {}
      void OnServerToClientMessage(Message&) {}
      void OnServerTrailingMetadata(ServerMetadata&) {}
      void OnFinalize(const grpc_call_final_info*) {}
    };
  };
  CallFilters::StackBuilder builder;
  Filter f;
  builder.Add(&f);
  auto stack = builder.Build();
  EXPECT_NE(stack, nullptr);
}

TEST(CallFiltersTest, UnaryCall) {
  struct Filter {
    struct Call {
      void OnClientInitialMetadata(ClientMetadata&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnClientInitialMetadata"));
      }
      void OnServerInitialMetadata(ServerMetadata&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnServerInitialMetadata"));
      }
      void OnClientToServerMessage(Message&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnClientToServerMessage"));
      }
      void OnClientToServerHalfClose(Filter* f) {
        f->steps.push_back(
            absl::StrCat(f->label, ":OnClientToServerHalfClose"));
      }
      void OnServerToClientMessage(Message&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnServerToClientMessage"));
      }
      void OnServerTrailingMetadata(ServerMetadata&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnServerTrailingMetadata"));
      }
      void OnFinalize(const grpc_call_final_info*, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnFinalize"));
      }
      std::unique_ptr<int> i = std::make_unique<int>(3);
    };

    const std::string label;
    std::vector<std::string>& steps;
  };
  std::vector<std::string> steps;
  Filter f1{"f1", steps};
  Filter f2{"f2", steps};
  CallFilters::StackBuilder builder;
  builder.Add(&f1);
  builder.Add(&f2);
  auto arena = SimpleArenaAllocator()->MakeArena();
  CallFilters filters(Arena::MakePooledForOverwrite<ClientMetadata>());
  filters.AddStack(builder.Build());
  filters.Start();
  promise_detail::Context<Arena> ctx(arena.get());
  StrictMock<MockActivity> activity;
  activity.Activate();
  // Pull client initial metadata
  auto pull_client_initial_metadata = filters.PullClientInitialMetadata();
  EXPECT_THAT(pull_client_initial_metadata(), IsReady());
  Mock::VerifyAndClearExpectations(&activity);
  // Push client to server message
  auto push_client_to_server_message = filters.PushClientToServerMessage(
      Arena::MakePooled<Message>(SliceBuffer(), 0));
  EXPECT_THAT(push_client_to_server_message(), IsPending());
  auto pull_client_to_server_message = filters.PullClientToServerMessage();
  // Pull client to server message, expect a wakeup
  EXPECT_WAKEUP(activity,
                EXPECT_THAT(pull_client_to_server_message(), IsReady()));
  // Push should be done
  EXPECT_THAT(push_client_to_server_message(), IsReady(Success{}));
  // Push server initial metadata
  filters.PushServerInitialMetadata(
      Arena::MakePooledForOverwrite<ServerMetadata>());
  auto pull_server_initial_metadata = filters.PullServerInitialMetadata();
  // Pull server initial metadata
  EXPECT_THAT(pull_server_initial_metadata(), IsReady());
  Mock::VerifyAndClearExpectations(&activity);
  // Push server to client message
  auto push_server_to_client_message = filters.PushServerToClientMessage(
      Arena::MakePooled<Message>(SliceBuffer(), 0));
  EXPECT_THAT(push_server_to_client_message(), IsPending());
  auto pull_server_to_client_message = filters.PullServerToClientMessage();
  // Pull server to client message, expect a wakeup
  EXPECT_WAKEUP(activity,
                EXPECT_THAT(pull_server_to_client_message(), IsReady()));
  // Push should be done
  EXPECT_THAT(push_server_to_client_message(), IsReady(Success{}));
  // Push server trailing metadata
  filters.PushServerTrailingMetadata(
      Arena::MakePooledForOverwrite<ServerMetadata>());
  // Pull server trailing metadata
  auto pull_server_trailing_metadata = filters.PullServerTrailingMetadata();
  // Should be done
  EXPECT_THAT(pull_server_trailing_metadata(), IsReady());
  filters.Finalize(nullptr);
  EXPECT_THAT(steps,
              ::testing::ElementsAre(
                  "f1:OnClientInitialMetadata", "f2:OnClientInitialMetadata",
                  "f1:OnClientToServerMessage", "f2:OnClientToServerMessage",
                  "f2:OnServerInitialMetadata", "f1:OnServerInitialMetadata",
                  "f2:OnServerToClientMessage", "f1:OnServerToClientMessage",
                  "f2:OnServerTrailingMetadata", "f1:OnServerTrailingMetadata",
                  "f1:OnFinalize", "f2:OnFinalize"));
}

TEST(CallFiltersTest, UnaryCallWithMultiStack) {
  struct Filter {
    struct Call {
      void OnClientInitialMetadata(ClientMetadata&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnClientInitialMetadata"));
      }
      void OnServerInitialMetadata(ServerMetadata&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnServerInitialMetadata"));
      }
      void OnClientToServerMessage(Message&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnClientToServerMessage"));
      }
      void OnClientToServerHalfClose(Filter* f) {
        f->steps.push_back(
            absl::StrCat(f->label, ":OnClientToServerHalfClose"));
      }
      void OnServerToClientMessage(Message&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnServerToClientMessage"));
      }
      void OnServerTrailingMetadata(ServerMetadata&, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnServerTrailingMetadata"));
      }
      void OnFinalize(const grpc_call_final_info*, Filter* f) {
        f->steps.push_back(absl::StrCat(f->label, ":OnFinalize"));
      }
      std::unique_ptr<int> i = std::make_unique<int>(3);
    };

    const std::string label;
    std::vector<std::string>& steps;
  };
  std::vector<std::string> steps;
  Filter f1{"f1", steps};
  Filter f2{"f2", steps};
  CallFilters::StackBuilder builder1;
  CallFilters::StackBuilder builder2;
  builder1.Add(&f1);
  builder2.Add(&f2);
  auto arena = SimpleArenaAllocator()->MakeArena();
  CallFilters filters(Arena::MakePooledForOverwrite<ClientMetadata>());
  filters.AddStack(builder1.Build());
  filters.AddStack(builder2.Build());
  filters.Start();
  promise_detail::Context<Arena> ctx(arena.get());
  StrictMock<MockActivity> activity;
  activity.Activate();
  // Pull client initial metadata
  auto pull_client_initial_metadata = filters.PullClientInitialMetadata();
  EXPECT_THAT(pull_client_initial_metadata(), IsReady());
  Mock::VerifyAndClearExpectations(&activity);
  // Push client to server message
  auto push_client_to_server_message = filters.PushClientToServerMessage(
      Arena::MakePooled<Message>(SliceBuffer(), 0));
  EXPECT_THAT(push_client_to_server_message(), IsPending());
  auto pull_client_to_server_message = filters.PullClientToServerMessage();
  // Pull client to server message, expect a wakeup
  EXPECT_WAKEUP(activity,
                EXPECT_THAT(pull_client_to_server_message(), IsReady()));
  // Push should be done
  EXPECT_THAT(push_client_to_server_message(), IsReady(Success{}));
  // Push server initial metadata
  filters.PushServerInitialMetadata(
      Arena::MakePooledForOverwrite<ServerMetadata>());
  auto pull_server_initial_metadata = filters.PullServerInitialMetadata();
  // Pull server initial metadata
  EXPECT_THAT(pull_server_initial_metadata(), IsReady());
  Mock::VerifyAndClearExpectations(&activity);
  // Push server to client message
  auto push_server_to_client_message = filters.PushServerToClientMessage(
      Arena::MakePooled<Message>(SliceBuffer(), 0));
  EXPECT_THAT(push_server_to_client_message(), IsPending());
  auto pull_server_to_client_message = filters.PullServerToClientMessage();
  // Pull server to client message, expect a wakeup
  EXPECT_WAKEUP(activity,
                EXPECT_THAT(pull_server_to_client_message(), IsReady()));
  // Push should be done
  EXPECT_THAT(push_server_to_client_message(), IsReady(Success{}));
  // Push server trailing metadata
  filters.PushServerTrailingMetadata(
      Arena::MakePooledForOverwrite<ServerMetadata>());
  // Pull server trailing metadata
  auto pull_server_trailing_metadata = filters.PullServerTrailingMetadata();
  // Should be done
  EXPECT_THAT(pull_server_trailing_metadata(), IsReady());
  filters.Finalize(nullptr);
  EXPECT_THAT(steps,
              ::testing::ElementsAre(
                  "f1:OnClientInitialMetadata", "f2:OnClientInitialMetadata",
                  "f1:OnClientToServerMessage", "f2:OnClientToServerMessage",
                  "f2:OnServerInitialMetadata", "f1:OnServerInitialMetadata",
                  "f2:OnServerToClientMessage", "f1:OnServerToClientMessage",
                  "f2:OnServerTrailingMetadata", "f1:OnServerTrailingMetadata",
                  "f1:OnFinalize", "f2:OnFinalize"));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc_tracer_init();
  return RUN_ALL_TESTS();
}
