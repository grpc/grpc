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

#include "src/core/util/latent_see.h"

#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <thread>

#include "absl/functional/function_ref.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/notification.h"

using testing::IsEmpty;

MATCHER_P2(HasStringFieldWithValue, field, value, "") {
  auto f = arg.find(field);
  if (f == arg.end()) {
    *result_listener << "does not have field " << field;
    return false;
  }
  if (f->second.type() != grpc_core::Json::Type::kString) {
    *result_listener << "field " << field << " is a "
                     << absl::StrCat(f->second.type());
    return false;
  }
  if (f->second.string() != value) {
    *result_listener << "field " << field << " is " << f->second.string();
    return false;
  }
  return true;
}

MATCHER_P(HasStringField, field, "") {
  auto f = arg.find(field);
  if (f == arg.end()) {
    *result_listener << "does not have field " << field;
    return false;
  }
  if (f->second.type() != grpc_core::Json::Type::kString) {
    *result_listener << "field " << field << " is a "
                     << absl::StrCat(f->second.type());
    return false;
  }
  return true;
}

MATCHER_P2(HasNumberFieldWithValue, field, value, "") {
  auto f = arg.find(field);
  if (f == arg.end()) {
    *result_listener << "does not have field " << field;
    return false;
  }
  if (f->second.type() != grpc_core::Json::Type::kNumber) {
    *result_listener << "field " << field << " is a "
                     << absl::StrCat(f->second.type());
    return false;
  }
  if (f->second.string() != absl::StrCat(value)) {
    *result_listener << "field " << field << " is " << f->second.string();
    return false;
  }
  return true;
}

MATCHER_P(HasNumberField, field, "") {
  auto f = arg.find(field);
  if (f == arg.end()) {
    *result_listener << "does not have field " << field;
    return false;
  }
  if (f->second.type() != grpc_core::Json::Type::kNumber) {
    *result_listener << "field " << field << " is a "
                     << absl::StrCat(f->second.type());
    return false;
  }
  return true;
}

namespace grpc_core {
namespace {

Json::Array RunAndReportJson(absl::FunctionRef<void()> fn) {
  Notification finish_scopes;
  std::string json;
  std::thread t([&]() {
    std::ostringstream out;
    {
      latent_see::JsonOutput output(out);
      latent_see::Collect(&finish_scopes, absl::Hours(24),
                          std::numeric_limits<size_t>::max(), &output);
    }
    json = out.str();
  });
  // wait for the collection to start
  absl::SleepFor(absl::Seconds(2));
  fn();
  latent_see::Flush();
  // let the collection thread catch up
  absl::SleepFor(absl::Seconds(2));
  finish_scopes.Notify();
  t.join();
  auto a = JsonParse(json);
  CHECK_OK(a);
  CHECK_EQ(a->type(), Json::Type::kArray);
  return a->array();
}

TEST(LatentSeeTest, EmptyCollectionWorks) {
  EXPECT_THAT(RunAndReportJson([]() {}), IsEmpty());
}

TEST(LatentSeeTest, ScopeWorks) {
  auto elems = RunAndReportJson([]() {
    GRPC_LATENT_SEE_ALWAYS_ON_SCOPE("foo");
    absl::SleepFor(absl::Milliseconds(5));
  });
  ASSERT_EQ(elems.size(), 1);
  ASSERT_EQ(elems[0].type(), Json::Type::kObject);
  auto obj = elems[0].object();
  EXPECT_THAT(obj, HasStringFieldWithValue("name", "foo"));
  EXPECT_THAT(obj, HasStringFieldWithValue("ph", "X"));
  EXPECT_THAT(obj, HasNumberFieldWithValue("tid", 1));
  EXPECT_THAT(obj, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj, HasNumberField("dur"));
  double dur;
  ASSERT_TRUE(absl::SimpleAtod(obj["dur"].string(), &dur));
  EXPECT_GE(dur, 5000.0);
  EXPECT_THAT(obj, HasNumberField("ts"));
}

TEST(LatentSeeTest, MarkWorks) {
  auto elems =
      RunAndReportJson([]() { GRPC_LATENT_SEE_ALWAYS_ON_MARK("bar"); });
  ASSERT_EQ(elems.size(), 1);
  ASSERT_EQ(elems[0].type(), Json::Type::kObject);
  auto obj = elems[0].object();
  EXPECT_THAT(obj, HasStringFieldWithValue("name", "bar"));
  EXPECT_THAT(obj, HasStringFieldWithValue("ph", "i"));
  EXPECT_THAT(obj, HasNumberFieldWithValue("tid", 1));
  EXPECT_THAT(obj, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj, HasNumberField("ts"));
}

TEST(LatentSeeTest, FlowWorks) {
  auto elems = RunAndReportJson([]() {
    std::thread([f = std::make_unique<latent_see::Flow>(latent_see::Flow(
                     GRPC_LATENT_SEE_METADATA("foo")))]() mutable {
      f.reset();
      latent_see::Flush();
    }).join();
  });
  ASSERT_EQ(elems.size(), 2);
  ASSERT_EQ(elems[0].type(), Json::Type::kObject);
  auto obj1 = elems[0].object();
  auto obj2 = elems[1].object();
  // Test phrasing ensures that the end (ph:f) gets reported
  // before the start (ph:s)
  EXPECT_THAT(obj1, HasStringFieldWithValue("name", "foo"));
  EXPECT_THAT(obj1, HasStringFieldWithValue("ph", "f"));
  EXPECT_THAT(obj1, HasNumberFieldWithValue("tid", 1));
  EXPECT_THAT(obj1, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj1, HasNumberField("ts"));
  EXPECT_THAT(obj2, HasStringFieldWithValue("name", "foo"));
  EXPECT_THAT(obj2, HasStringFieldWithValue("ph", "s"));
  EXPECT_THAT(obj2, HasNumberFieldWithValue("tid", 2));
  EXPECT_THAT(obj2, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj2, HasNumberField("ts"));
}

}  // namespace
}  // namespace grpc_core
