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

#include "src/core/lib/promise/try_concurrently.h"

#include <algorithm>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace grpc_core {

class PromiseFactory {
 public:
  // Create a promise that resolves to Ok but has a memory allocation (to verify
  // destruction)
  // tag should have static lifetime (i.e. pass a string literal here).
  auto OkPromise(absl::string_view tag) {
    return [this, tag,
            p = std::make_unique<absl::Status>(absl::OkStatus())]() mutable {
      order_.push_back(tag);
      return std::move(*p);
    };
  }

  // Create a promise that never resolves and carries a memory allocation
  // tag should have static lifetime (i.e. pass a string literal here).
  auto NeverPromise(absl::string_view tag) {
    return
        [this, tag, p = std::make_unique<Pending>()]() -> Poll<absl::Status> {
          order_.push_back(tag);
          return *p;
        };
  }

  // Create a promise that fails and carries a memory allocation
  // tag should have static lifetime (i.e. pass a string literal here).
  auto FailPromise(absl::string_view tag) {
    return [this, p = std::make_unique<absl::Status>(absl::UnknownError(tag)),
            tag]() mutable {
      order_.push_back(tag);
      return std::move(*p);
    };
  }

  // Finish one round and return a vector of strings representing which promises
  // were polled and in which order.
  std::vector<absl::string_view> Finish() { return std::exchange(order_, {}); }

 private:
  std::vector<absl::string_view> order_;
};

std::ostream& operator<<(std::ostream& out, const Poll<absl::Status>& p) {
  return out << PollToString(
             p, [](const absl::Status& s) { return s.ToString(); });
}

TEST(TryConcurrentlyTest, Immediate) {
  PromiseFactory pf;
  auto a = TryConcurrently(pf.OkPromise("1"));
  EXPECT_EQ(a(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"1"}));
  auto b = TryConcurrently(pf.OkPromise("1")).NecessaryPush(pf.OkPromise("2"));
  EXPECT_EQ(b(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"2", "1"}));
  auto c = TryConcurrently(pf.OkPromise("1")).NecessaryPull(pf.OkPromise("2"));
  EXPECT_EQ(c(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"1", "2"}));
  auto d = TryConcurrently(pf.OkPromise("1"))
               .NecessaryPull(pf.OkPromise("2"))
               .NecessaryPush(pf.OkPromise("3"));
  EXPECT_EQ(d(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"3", "1", "2"}));
  auto e = TryConcurrently(pf.OkPromise("1")).Push(pf.NeverPromise("2"));
  EXPECT_EQ(e(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"2", "1"}));
  auto f = TryConcurrently(pf.OkPromise("1")).Pull(pf.NeverPromise("2"));
  EXPECT_EQ(f(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"1", "2"}));
}

TEST(TryConcurrentlyTest, Paused) {
  PromiseFactory pf;
  auto a = TryConcurrently(pf.NeverPromise("1"));
  EXPECT_EQ(a(), Poll<absl::Status>(Pending{}));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"1"}));
  auto b =
      TryConcurrently(pf.OkPromise("1")).NecessaryPush(pf.NeverPromise("2"));
  EXPECT_EQ(b(), Poll<absl::Status>(Pending{}));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"2", "1"}));
  auto c =
      TryConcurrently(pf.OkPromise("1")).NecessaryPull(pf.NeverPromise("2"));
  EXPECT_EQ(c(), Poll<absl::Status>(Pending{}));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"1", "2"}));
}

TEST(TryConcurrentlyTest, OneFailed) {
  PromiseFactory pf;
  auto a = TryConcurrently(pf.FailPromise("bah"));
  EXPECT_EQ(a(), Poll<absl::Status>(absl::UnknownError("bah")));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"bah"}));
  auto b = TryConcurrently(pf.NeverPromise("1"))
               .NecessaryPush(pf.FailPromise("humbug"));
  EXPECT_EQ(b(), Poll<absl::Status>(absl::UnknownError("humbug")));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"humbug"}));
  auto c = TryConcurrently(pf.NeverPromise("1"))
               .NecessaryPull(pf.FailPromise("wha"));
  EXPECT_EQ(c(), Poll<absl::Status>(absl::UnknownError("wha")));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>({"1", "wha"}));
}

TEST(TryConcurrently, MuchPush) {
  PromiseFactory pf;
  auto p = TryConcurrently(pf.OkPromise("1"))
               .NecessaryPush(pf.OkPromise("2"))
               .NecessaryPush(pf.OkPromise("3"))
               .NecessaryPush(pf.OkPromise("4"))
               .NecessaryPush(pf.OkPromise("5"))
               .NecessaryPush(pf.OkPromise("6"))
               .NecessaryPush(pf.OkPromise("7"))
               .NecessaryPush(pf.OkPromise("8"));
  EXPECT_EQ(p(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>(
                             {"8", "7", "6", "5", "4", "3", "2", "1"}));
}

TEST(TryConcurrently, SoPull) {
  PromiseFactory pf;
  auto p = TryConcurrently(pf.OkPromise("1"))
               .NecessaryPull(pf.OkPromise("2"))
               .NecessaryPull(pf.OkPromise("3"))
               .NecessaryPull(pf.OkPromise("4"))
               .NecessaryPull(pf.OkPromise("5"))
               .NecessaryPull(pf.OkPromise("6"))
               .NecessaryPull(pf.OkPromise("7"))
               .NecessaryPull(pf.OkPromise("8"));
  EXPECT_EQ(p(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_EQ(pf.Finish(), std::vector<absl::string_view>(
                             {"1", "8", "7", "6", "5", "4", "3", "2"}));
}

// A pointer to an int designed to cause a double free if it's double destructed
// (to flush out bugs)
class ProblematicPointer {
 public:
  ProblematicPointer() : p_(new int(0)) {}
  ~ProblematicPointer() { delete p_; }
  ProblematicPointer(const ProblematicPointer&) = delete;
  ProblematicPointer& operator=(const ProblematicPointer&) = delete;
  // NOLINTNEXTLINE: we want to allocate during move
  ProblematicPointer(ProblematicPointer&& other) : p_(new int(*other.p_ + 1)) {}
  ProblematicPointer& operator=(ProblematicPointer&& other) = delete;

 private:
  int* p_;
};

TEST(TryConcurrentlyTest, MoveItMoveIt) {
  auto a =
      TryConcurrently([x = ProblematicPointer()]() { return absl::OkStatus(); })
          .NecessaryPull(
              [x = ProblematicPointer()]() { return absl::OkStatus(); })
          .NecessaryPush(
              [x = ProblematicPointer()]() { return absl::OkStatus(); })
          .Push([x = ProblematicPointer()]() { return absl::OkStatus(); })
          .Pull([x = ProblematicPointer()]() { return absl::OkStatus(); });
  auto b = std::move(a);
  auto c = std::move(b);
  c();
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
