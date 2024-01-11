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

#include "src/core/lib/transport/call_filters.h"

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {

namespace {
void* Offset(void* base, size_t amt) { return static_cast<char*>(base) + amt; }
}  // namespace

////////////////////////////////////////////////////////////////////////////////
// Layout

namespace filters_detail {

TEST(LayoutTest, Empty) {
  Layout<FallibleOperator<ClientMetadataHandle>> l;
  EXPECT_EQ(l.ops.size(), 0u);
  EXPECT_EQ(l.promise_size, 0u);
  EXPECT_EQ(l.promise_alignment, 0u);
}

TEST(LayoutTest, Add) {
  Layout<FallibleOperator<ClientMetadataHandle>> l;
  l.Add(1, 4,
        FallibleOperator<ClientMetadataHandle>{&l, 120, nullptr, nullptr,
                                               nullptr});
  EXPECT_EQ(l.ops.size(), 1u);
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
  EXPECT_EQ(d.call_data_alignment, 0u);
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
  EXPECT_EQ(d.filters.size(), 1u);
  // Check channel data
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  // Check call offsets
  EXPECT_EQ(d.filters[0].call_offset, 0);
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
  EXPECT_EQ(d.filters.size(), 1u);
  // Check channel data
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  // Check call offsets
  EXPECT_EQ(d.filters[0].call_offset, 0);
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
  EXPECT_EQ(d.filters.size(), 2u);
  // Check channel data
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[1].channel_data, &f2);
  // Check call offsets
  EXPECT_EQ(d.filters[0].call_offset, 0);
  EXPECT_EQ(d.filters[1].call_offset, sizeof(void*));
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
  EXPECT_EQ(d.filters.size(), 2u);
  // Check channel data
  EXPECT_EQ(d.filters[0].channel_data, &f2);
  EXPECT_EQ(d.filters[1].channel_data, &f1);
  // Check call offsets
  EXPECT_EQ(d.filters[0].call_offset, 0);
  EXPECT_EQ(d.filters[1].call_offset, sizeof(void*));
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
  EXPECT_EQ(d.filters.size(), 6u);
  // Check channel data
  EXPECT_EQ(d.filters[0].channel_data, &f1a);
  EXPECT_EQ(d.filters[1].channel_data, &f2a);
  EXPECT_EQ(d.filters[2].channel_data, &f2b);
  EXPECT_EQ(d.filters[3].channel_data, &f2c);
  EXPECT_EQ(d.filters[4].channel_data, &f2d);
  EXPECT_EQ(d.filters[5].channel_data, &f1b);
  // Check call offsets
  EXPECT_EQ(d.filters[0].call_offset, 0);
  EXPECT_EQ(d.filters[1].call_offset, 0);
  EXPECT_EQ(d.filters[2].call_offset, 0);
  EXPECT_EQ(d.filters[3].call_offset, 0);
  EXPECT_EQ(d.filters[4].call_offset, 0);
  EXPECT_EQ(d.filters[5].call_offset, 1);
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, 0);
  void* p = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(p, &f1);
  EXPECT_EQ(*static_cast<Filter1::Call*>(p)->p, 42);
  d.filters[0].call_destroy(p);
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, 0);
  void* p = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(p, &f1);
  EXPECT_EQ(*static_cast<Filter1::Call*>(p)->p, &f1);
  d.filters[0].call_destroy(p);
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, nullptr, d.client_initial_metadata.ops[0].channel_data,
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, nullptr, d.client_initial_metadata.ops[0].channel_data,
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(call_data, &f1);
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  d.filters[0].call_destroy(call_data);
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(call_data, &f1);
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  d.filters[0].call_destroy(call_data);
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
                     : ServerMetadataFromStatus(absl::CancelledError());
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(call_data, &f1);
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  d.filters[0].call_destroy(call_data);
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
                     : ServerMetadataFromStatus(absl::CancelledError());
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(call_data, &f1);
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      nullptr, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok, nullptr);
  EXPECT_EQ(r.value().error->get(GrpcStatusMetadata()), GRPC_STATUS_CANCELLED);
  d.filters[0].call_destroy(call_data);
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(call_data, &f1);
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  d.client_initial_metadata.ops[0].early_destroy(promise_data);
  // ASAN will trigger if things aren't cleaned up
  d.filters[0].call_destroy(call_data);
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.client_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_initial_metadata.ops[0].channel_data, &f1);
  // Check promise init
  void* call_data = gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  d.filters[0].call_init(call_data, &f1);
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
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
  md = Arena::MakePooled<ClientMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  r = d.client_initial_metadata.ops[0].promise_init(
      promise_data, call_data, d.client_initial_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_FALSE(r.ready());
  d.client_initial_metadata.ops[0].early_destroy(promise_data);
  // ASAN will trigger if things aren't cleaned up
  d.filters[0].call_destroy(call_data);
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.server_initial_metadata.ops.size(), 1u);
  EXPECT_EQ(d.server_initial_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.server_initial_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.server_initial_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.server_initial_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ServerMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.server_initial_metadata.ops[0].promise_init(
      nullptr, nullptr, d.server_initial_metadata.ops[0].channel_data,
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.client_to_server_messages.ops.size(), 1u);
  EXPECT_EQ(d.client_to_server_messages.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.client_to_server_messages.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.client_to_server_messages.ops[0].poll, nullptr);
  EXPECT_EQ(d.client_to_server_messages.ops[0].early_destroy, nullptr);
  // Check promise init
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto message = Arena::MakePooled<Message>(SliceBuffer(), 0);
  auto r = d.client_to_server_messages.ops[0].promise_init(
      nullptr, nullptr, d.client_to_server_messages.ops[0].channel_data,
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.server_to_client_messages.ops.size(), 1u);
  EXPECT_EQ(d.server_to_client_messages.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.server_to_client_messages.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.server_to_client_messages.ops[0].poll, nullptr);
  EXPECT_EQ(d.server_to_client_messages.ops[0].early_destroy, nullptr);
  // Check promise init
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto message = Arena::MakePooled<Message>(SliceBuffer(), 0);
  auto r = d.server_to_client_messages.ops[0].promise_init(
      nullptr, nullptr, d.server_to_client_messages.ops[0].channel_data,
      std::move(message));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->flags(), 1u);
}

TEST(StackDataTest, InstantServerTrailingMetadataReturningVoid) {
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.server_trailing_metadata.ops.size(), 1u);
  EXPECT_EQ(d.server_trailing_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.server_trailing_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.server_trailing_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.server_trailing_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ServerMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.server_trailing_metadata.ops[0].promise_init(
      nullptr, nullptr, d.server_trailing_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value()->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
}

TEST(StackDataTest,
     InstantServerTrailingMetadataReturningVoidTakingChannelPtr) {
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
  EXPECT_EQ(d.filters.size(), 1u);
  EXPECT_EQ(d.filters[0].channel_data, &f1);
  EXPECT_EQ(d.filters[0].call_offset, call_offset);
  EXPECT_EQ(d.server_trailing_metadata.ops.size(), 1u);
  EXPECT_EQ(d.server_trailing_metadata.ops[0].call_offset, call_offset);
  EXPECT_EQ(d.server_trailing_metadata.ops[0].channel_data, &f1);
  // Instant => no poll/early destroy
  EXPECT_EQ(d.server_trailing_metadata.ops[0].poll, nullptr);
  EXPECT_EQ(d.server_trailing_metadata.ops[0].early_destroy, nullptr);
  // Check promise init
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ServerMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r = d.server_trailing_metadata.ops[0].promise_init(
      nullptr, nullptr, d.server_trailing_metadata.ops[0].channel_data,
      std::move(md));
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value()->get_pointer(HttpPathMetadata())->as_string_view(),
            "hello");
  EXPECT_THAT(f1.v, ::testing::ElementsAre(42));
}

}  // namespace filters_detail

///////////////////////////////////////////////////////////////////////////////
// PipeTransformer

namespace filters_detail {

TEST(PipeTransformerTest, NoOp) {
  PipeTransformer<ClientMetadataHandle> pipe;
  EXPECT_FALSE(pipe.IsRunning());
}

TEST(PipeTransformerTest, InstantTwo) {
  class Filter1 {
   public:
    class Call {
     public:
      absl::Status OnClientInitialMetadata(ClientMetadata& md) {
        if (md.get_pointer(HttpPathMetadata()) != nullptr) {
          md.Set(HttpPathMetadata(), Slice::FromStaticString("world"));
        } else {
          md.Set(HttpPathMetadata(), Slice::FromStaticString("hello"));
        }
        bool first = std::exchange(first_, false);
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
  EXPECT_EQ(d.filters.size(), 2u);
  EXPECT_EQ(d.client_initial_metadata.ops.size(), 2u);
  void* call_data1 =
      gpr_malloc_aligned(d.call_data_size, d.call_data_alignment);
  void* call_data2 = Offset(call_data1, d.filters[1].call_offset);
  d.filters[0].call_init(call_data1, &f1);
  d.filters[1].call_init(call_data2, &f2);
  PipeTransformer<ClientMetadataHandle> transformer;
  auto memory_allocator =
      MakeMemoryQuota("test-quota")->CreateMemoryAllocator("foo");
  auto arena = MakeScopedArena(1024, &memory_allocator);
  auto md = Arena::MakePooled<ServerMetadata>(arena.get());
  EXPECT_EQ(md->get_pointer(HttpPathMetadata()), nullptr);
  auto r =
      transformer.Start(&d.client_initial_metadata, std::move(md), call_data1);
  EXPECT_TRUE(r.ready());
  EXPECT_EQ(r.value().ok->get_pointer(HttpPathMetadata())->as_string_view(),
            "world");
  d.filters[1].call_destroy(call_data2);
  d.filters[0].call_destroy(call_data1);
  gpr_free_aligned(call_data1);
}

}  // namespace filters_detail

///////////////////////////////////////////////////////////////////////////////
// InfalliblePipeTransformer

namespace filters_detail {

TEST(InfalliblePipeTransformer, NoOp) {
  PipeTransformer<ServerMetadataHandle> pipe;
  EXPECT_FALSE(pipe.IsRunning());
}

}  // namespace filters_detail

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
