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
#include "src/core/lib/promise/promise.h"

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
  std::string execution_order;
  auto all_ok1 = AssertResultType<absl::Status>(AllOk<absl::Status>(
      [&execution_order]() -> Poll<absl::Status> {
        absl::StrAppend(&execution_order, "1");
        return absl::OkStatus();
      },
      [&execution_order]() -> Poll<StatusFlag> {
        absl::StrAppend(&execution_order, "2");
        return Success{};
      },
      [i = 1, &execution_order]() mutable -> Poll<StatusFlag> {
        if (i == 0) {
          absl::StrAppend(&execution_order, "3");
          return Success{};
        }
        absl::StrAppend(&execution_order, "3_P");
        i--;
        return Pending{};
      },
      [i = 2, &execution_order]() mutable -> Poll<absl::Status> {
        if (i == 0) {
          absl::StrAppend(&execution_order, "4");
          return absl::OkStatus();
        }
        absl::StrAppend(&execution_order, "4_P");
        i--;
        return Pending{};
      }));
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "123_P4_P");

  execution_order.clear();
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "34_P");

  execution_order.clear();
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(absl::OkStatus()));
  EXPECT_STREQ(execution_order.c_str(), "4");
}

TEST(AllOkTest, WithMixedTypesFailure) {
  std::string execution_order;
  auto all_ok1 = AssertResultType<absl::Status>(AllOk<absl::Status>(
      [&execution_order]() -> Poll<absl::Status> {
        absl::StrAppend(&execution_order, "1");
        return absl::OkStatus();
      },
      [i = 2, &execution_order]() mutable -> Poll<absl::Status> {
        if (i == 0) {
          absl::StrAppend(&execution_order, "4");
          return absl::UnknownError("failed");
        }
        absl::StrAppend(&execution_order, "4_P");
        i--;
        return Pending{};
      },
      [&execution_order]() -> Poll<StatusFlag> {
        absl::StrAppend(&execution_order, "2");
        return Success{};
      },
      [i = 1, &execution_order]() mutable -> Poll<StatusFlag> {
        if (i == 0) {
          absl::StrAppend(&execution_order, "3");
          return Success{};
        }
        absl::StrAppend(&execution_order, "3_P");
        i--;
        return Pending{};
      }));

  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "14_P23_P");

  execution_order.clear();
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "4_P3");

  execution_order.clear();
  // The failed promise here returned an absl::Status, and the AllOk combinator
  // will propagate the same failure status.
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(absl::UnknownError("failed")));
  EXPECT_STREQ(execution_order.c_str(), "4");
}

TEST(AllOkTest, WithMixedTypesFailure2) {
  std::string execution_order;
  auto all_ok1 = AssertResultType<absl::Status>(AllOk<absl::Status>(
      [&execution_order]() -> Poll<absl::Status> {
        absl::StrAppend(&execution_order, "1");
        return absl::OkStatus();
      },
      [&execution_order]() -> Poll<StatusFlag> {
        absl::StrAppend(&execution_order, "2");
        return Success{};
      },
      [i = 1, &execution_order]() mutable -> Poll<StatusFlag> {
        if (i == 0) {
          absl::StrAppend(&execution_order, "3");
          return Failure{};
        }
        absl::StrAppend(&execution_order, "3_P");
        i--;
        return Pending{};
      },
      [i = 2, &execution_order]() mutable -> Poll<absl::Status> {
        if (i == 0) {
          absl::StrAppend(&execution_order, "4");
          return absl::OkStatus();
        }
        absl::StrAppend(&execution_order, "4_P");
        i--;
        return Pending{};
      }));
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(Pending{}));
  EXPECT_STREQ(execution_order.c_str(), "123_P4_P");

  execution_order.clear();
  // The failed promise here returned a StatusFlag, but the AllOk combinator
  // will cast it to absl::CancelledError().
  EXPECT_EQ(all_ok1(), Poll<absl::Status>(absl::CancelledError()));
  EXPECT_STREQ(execution_order.c_str(), "3");
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
