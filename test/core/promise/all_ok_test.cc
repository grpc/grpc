// Copyright 2021 gRPC authors.
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

#include "src/core/lib/promise/all_ok.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/utility/utility.h"
#include "gtest/gtest.h"

namespace grpc_core {

template <typename T>
struct AllOkTestTraits;

template <>
struct AllOkTestTraits<StatusFlag> {
  using PollType = Poll<StatusFlag>;
  using P = std::function<Poll<StatusFlag>()>;
  static PollType failed() { return Failure{}; }
  static PollType succeeded() { return Success{}; }
};

template <>
struct AllOkTestTraits<absl::Status> {
  using PollType = Poll<absl::Status>;
  using P = std::function<Poll<absl::Status>()>;
  static PollType failed() { return absl::CancelledError(); }
  static PollType succeeded() { return absl::OkStatus(); }
};

template <typename T>
class AllOkTest : public ::testing::Test {
  using Traits = AllOkTestTraits<T>;
  using PollType = typename Traits::PollType;
  using P = typename Traits::P;

 public:
  PollType pending() { return Pending{}; }
  P always_pending() {
    return []() -> PollType { return Pending{}; };
  }

  P instant_success() {
    return []() { return Traits::succeeded(); };
  }
  P instant_fail() {
    return []() { return Traits::failed(); };
  }
  P pending_success(int i) {
    return [i]() mutable -> PollType {
      if (i == 0) {
        return Traits::succeeded();
      }
      i--;
      return Pending{};
    };
  }
  P pending_fail(int i) {
    return [i]() mutable -> PollType {
      if (i == 0) {
        return Traits::failed();
      }
      i--;
      return Pending{};
    };
  }
  PollType failed() { return Traits::failed(); }
  PollType succeeded() { return Traits::succeeded(); }
};

using AllOkTestTypes = ::testing::Types<StatusFlag, absl::Status>;
TYPED_TEST_SUITE(AllOkTest, AllOkTestTypes);

TYPED_TEST(AllOkTest, Join2) {
  EXPECT_EQ(AllOk<TypeParam>(this->instant_fail(), this->instant_fail())(),
            this->failed());
  EXPECT_EQ(AllOk<TypeParam>(this->instant_fail(), this->instant_success())(),
            this->failed());
  EXPECT_EQ(AllOk<TypeParam>(this->instant_success(), this->instant_fail())(),
            this->failed());
  EXPECT_EQ(
      AllOk<TypeParam>(this->instant_success(), this->instant_success())(),
      this->succeeded());
}

TYPED_TEST(AllOkTest, WithPendingFailed) {
  auto all_ok1 =
      AllOk<TypeParam>(this->pending_fail(1), this->always_pending(),
                       this->instant_success(), this->pending_success(1));
  EXPECT_EQ(all_ok1(), this->pending());
  EXPECT_EQ(all_ok1(), this->failed());
}

TYPED_TEST(AllOkTest, WithPendingSuccess) {
  auto all_ok1 =
      AllOk<TypeParam>(this->pending_success(1), this->instant_success(),
                       this->pending_success(2));
  EXPECT_EQ(all_ok1(), this->pending());
  EXPECT_EQ(all_ok1(), this->pending());
  EXPECT_EQ(all_ok1(), this->succeeded());
}

TYPED_TEST(AllOkTest, AllOkIter) {
  std::vector<int> v = {1, 2, 3};
  auto all_ok =
      AllOkIter<TypeParam>(v.begin(), v.end(), [cnt = 1, this](int i) mutable {
        if (i != 1 || cnt == 0) {
          return this->succeeded();
        }
        cnt--;
        return this->pending();
      });
  EXPECT_EQ(all_ok(), this->pending());
  EXPECT_EQ(all_ok(), this->succeeded());
}

TEST(AllOkTest, WithMixedTypesSuccess) {
  auto all_ok1 = AllOk<absl::Status>(
      []() -> Poll<absl::Status> { return absl::OkStatus(); },
      []() -> Poll<StatusFlag> { return Success{}; },
      [i = 1]() mutable -> Poll<StatusFlag> {
        if (i == 0) {
          return Success{};
        }
        i--;
        return Pending{};
      },
      [i = 2]() mutable -> Poll<absl::Status> {
        if (i == 0) {
          return absl::OkStatus();
        }
        i--;
        return Pending{};
      });
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(absl::OkStatus()));
}

TEST(AllOkTest, WithMixedTypesFailure) {
  auto all_ok1 = AllOk<absl::Status>(
      []() -> Poll<absl::Status> { return absl::OkStatus(); },
      []() -> Poll<StatusFlag> { return Success{}; },
      [i = 1]() mutable -> Poll<StatusFlag> {
        if (i == 0) {
          return Success{};
        }
        i--;
        return Pending{};
      },
      [i = 2]() mutable -> Poll<absl::Status> {
        if (i == 0) {
          return absl::UnknownError("failed");
        }
        i--;
        return Pending{};
      });
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  // The failed promise here returned an absl::Status, and the AllOk combinator
  // will propagate the same failure status.
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(absl::UnknownError("failed")));
}

TEST(AllOkTest, WithMixedTypesFailure2) {
  auto all_ok1 = AllOk<absl::Status>(
      []() -> Poll<absl::Status> { return absl::OkStatus(); },
      []() -> Poll<StatusFlag> { return Success{}; },
      [i = 1]() mutable -> Poll<StatusFlag> {
        if (i == 0) {
          return Failure{};
        }
        i--;
        return Pending{};
      },
      [i = 2]() mutable -> Poll<absl::Status> {
        if (i == 0) {
          return absl::OkStatus();
        }
        i--;
        return Pending{};
      });
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  // The failed promise here returned a StatusFlag, but the AllOk combinator
  // will cast it to absl::CancelledError().
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(absl::CancelledError()));
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
