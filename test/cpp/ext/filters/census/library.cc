//
//
// Copyright 2023 gRPC authors.
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

#include "test/cpp/ext/filters/census/library.h"

namespace grpc {
namespace testing {

const ::opencensus::tags::TagKey TEST_TAG_KEY =
    ::opencensus::tags::TagKey::Register("my_key");
const char* TEST_TAG_VALUE = "my_value";
const char* kExpectedTraceIdKey = "expected_trace_id";

ExportedTracesRecorder* traces_recorder_ = new ExportedTracesRecorder();

}  // namespace testing
}  // namespace grpc
