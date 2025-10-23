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
#include <type_traits>

#include "src/core/ext/transport/chttp2/transport/http2_ztrace_collector.h"
#include "src/core/util/function_signature.h"
#include "src/core/util/json/json_reader.h"
#include "src/core/util/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

using testing::IsEmpty;

inline constexpr absl::string_view kHeaderTraceFalse =
    grpc_core::TypeName<grpc_core::H2HeaderTrace<false>>();
inline constexpr absl::string_view kHeaderTraceTrue =
    grpc_core::TypeName<grpc_core::H2HeaderTrace<true>>();

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

MATCHER_P2(HasBooleanFieldWithValue, field, value, "") {
  auto f = arg.find(field);
  if (f == arg.end()) {
    *result_listener << "does not have field " << field;
    return false;
  }
  if (f->second.type() != grpc_core::Json::Type::kBoolean) {
    *result_listener << "field " << field << " is a "
                     << absl::StrCat(f->second.type());
    return false;
  }
  if (f->second.boolean() != value) {
    *result_listener << "field " << field << " is " << f->second.boolean();
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

Json::Array RunAndReportJson(absl::FunctionRef<void()> fn,
                             Notification* wait_for_start = nullptr) {
  Notification finish_scopes;
  std::string json;
  std::thread t([&]() {
    std::ostringstream out;
    {
      if (wait_for_start != nullptr) {
        wait_for_start->WaitForNotification();
      }
      latent_see::JsonOutput output(out);
      latent_see::Collect(&finish_scopes, absl::Hours(24),
                          std::numeric_limits<size_t>::max(), &output);
    }
    json = out.str();
  });
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

void WaitForCollector() {
  while (true) {
    latent_see::Appender appender;
    if (appender.Enabled()) break;
    absl::SleepFor(absl::Milliseconds(1));
  }
  // After collector is enabled, we still sleep for 2*kMaxBackoff.
  absl::SleepFor(absl::Seconds(1));
}

TEST(LatentSeeTest, EmptyCollectionWorks) {
  EXPECT_THAT(RunAndReportJson([]() {}), IsEmpty());
}

TEST(LatentSeeTest, ScopeWorks) {
  auto elems = RunAndReportJson([&]() {
    WaitForCollector();
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
  auto elems = RunAndReportJson([&]() {
    WaitForCollector();
    GRPC_LATENT_SEE_ALWAYS_ON_MARK("bar");
  });
  ASSERT_EQ(elems.size(), 1);
  ASSERT_EQ(elems[0].type(), Json::Type::kObject);
  auto obj = elems[0].object();
  EXPECT_THAT(obj, HasStringFieldWithValue("name", "bar"));
  EXPECT_THAT(obj, HasStringFieldWithValue("ph", "i"));
  EXPECT_THAT(obj, HasNumberFieldWithValue("tid", 1));
  EXPECT_THAT(obj, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj, HasNumberField("ts"));
}

TEST(LatentSeeTest, ExtraEventMarkWorks) {
  auto elems = RunAndReportJson([&]() {
    WaitForCollector();
    // Mix set of mark and extra event marks.
    GRPC_LATENT_SEE_ALWAYS_ON_MARK("bar");
    H2HeaderTrace<false> trace = {1, false, false, false, 1024};
    auto h2_read_trace_producer = []() {
      H2HeaderTrace<true> trace = {2, false, false, true, 4096};
      return trace;
    };
    using ResultType = std::result_of<decltype(h2_read_trace_producer)()>::type;
    GRPC_LATENT_SEE_ALWAYS_ON_MARK_EXTRA_EVENT(H2HeaderTrace<false>, trace);
    GRPC_LATENT_SEE_ALWAYS_ON_MARK_EXTRA_EVENT(ResultType,
                                               h2_read_trace_producer());
  });
  ASSERT_EQ(elems.size(), 3);
  ASSERT_EQ(elems[0].type(), Json::Type::kObject);
  auto obj_0 = elems[0].object();
  EXPECT_THAT(obj_0, HasStringFieldWithValue("name", "bar"));
  EXPECT_THAT(obj_0, HasStringFieldWithValue("ph", "i"));
  EXPECT_THAT(obj_0, HasNumberFieldWithValue("tid", 1));
  EXPECT_THAT(obj_0, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj_0, HasNumberField("ts"));

  ASSERT_EQ(elems[1].type(), Json::Type::kObject);
  ASSERT_EQ(elems[2].type(), Json::Type::kObject);
  auto obj_1 = elems[1].object();
  auto obj_2 = elems[2].object();
  // obj_1
  EXPECT_THAT(obj_1, HasStringFieldWithValue("name", kHeaderTraceFalse));
  EXPECT_THAT(obj_1, HasStringFieldWithValue("ph", "i"));
  EXPECT_THAT(obj_1, HasNumberFieldWithValue("tid", 1));
  EXPECT_THAT(obj_1, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj_1, HasNumberField("ts"));
  ASSERT_EQ(obj_1.find("args")->second.type(), Json::Type::kObject);
  auto args_1 = obj_1.find("args")->second.object();
  EXPECT_THAT(args_1, HasNumberFieldWithValue("stream_id", 1));
  EXPECT_THAT(args_1, HasBooleanFieldWithValue("end_headers", false));
  EXPECT_THAT(args_1, HasStringFieldWithValue("frame_type", "HEADERS"));
  EXPECT_THAT(args_1, HasBooleanFieldWithValue("read", false));
  EXPECT_THAT(args_1, HasNumberFieldWithValue("payload_length", 1024));

  // obj2
  EXPECT_THAT(obj_2, HasStringFieldWithValue("name", kHeaderTraceTrue));
  EXPECT_THAT(obj_2, HasStringFieldWithValue("ph", "i"));
  EXPECT_THAT(obj_2, HasNumberFieldWithValue("tid", 1));
  EXPECT_THAT(obj_2, HasNumberFieldWithValue("pid", 0));
  EXPECT_THAT(obj_2, HasNumberField("ts"));
  ASSERT_EQ(obj_2.find("args")->second.type(), Json::Type::kObject);
  auto args_2 = obj_2.find("args")->second.object();
  EXPECT_THAT(args_2, HasNumberFieldWithValue("stream_id", 2));
  EXPECT_THAT(args_2, HasBooleanFieldWithValue("end_headers", false));
  EXPECT_THAT(args_2, HasStringFieldWithValue("frame_type", "CONTINUATION"));
  EXPECT_THAT(args_2, HasBooleanFieldWithValue("read", true));
  EXPECT_THAT(args_2, HasNumberFieldWithValue("payload_length", 4096));
}

TEST(LatentSeeTest, FlowWorks) {
  auto elems = RunAndReportJson([&]() {
    WaitForCollector();
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

TEST(LatentSeeTest, FlowWorksAppenderStartsLate) {
  Notification wait_for_start;
  auto elems = RunAndReportJson(
      [&]() {
        std::thread([&, f = std::make_unique<latent_see::Flow>(latent_see::Flow(
                            GRPC_LATENT_SEE_METADATA("foo")))]() mutable {
          wait_for_start.Notify();
          WaitForCollector();
          f->Begin();
          f->End();
          f.reset();
          latent_see::Flush();
        }).join();
      },
      /*wait_for_start=*/&wait_for_start);
  ASSERT_EQ(elems.size(), 2);
}

}  // namespace
}  // namespace grpc_core
