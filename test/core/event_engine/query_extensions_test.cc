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
<<<<<<< HEAD
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/query_extensions.h"

=======
#include "src/core/lib/event_engine/query_extensions.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/port_platform.h>

>>>>>>> 078d94981ac22dc30fc6cd7ee4accc62602c86d8
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "gtest/gtest.h"
<<<<<<< HEAD

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/gprpp/crash.h"

namespace grpc_event_engine::experimental {
=======
#include "src/core/lib/gprpp/crash.h"

namespace grpc_event_engine {
namespace experimental {
>>>>>>> 078d94981ac22dc30fc6cd7ee4accc62602c86d8
namespace {

template <int i>
class TestExtension {
 public:
  TestExtension() = default;
  ~TestExtension() = default;

<<<<<<< HEAD
  static constexpr std::string EndpointExtensionName() {
=======
  static std::string EndpointExtensionName() {
>>>>>>> 078d94981ac22dc30fc6cd7ee4accc62602c86d8
    return "grpc.test.test_extension" + std::to_string(i);
  }

  int GetValue() const { return val_; }

 private:
  int val_ = i;
};

class ExtendedTestEndpoint
    : public ExtendedEndpoint<TestExtension<0>, TestExtension<1>,
                              TestExtension<2>> {
 public:
  ExtendedTestEndpoint() = default;
  ~ExtendedTestEndpoint() = default;
  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override {
    grpc_core::Crash("Not implemented");
  };
  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override {
    grpc_core::Crash("Not implemented");
  }
  /// Returns an address in the format described in DNSResolver. The returned
  /// values are expected to remain valid for the life of the Endpoint.
  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    grpc_core::Crash("Not implemented");
  }
  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    grpc_core::Crash("Not implemented");
  };
};

TEST(QueryExtensionsTest, EndpointSupportsMultipleExtensions) {
  ExtendedTestEndpoint endpoint;
<<<<<<< HEAD
  TestExtension<0>* extension_0 = QueryExtension<TestExtension<0>>(
      static_cast<EventEngine::Endpoint*>(&endpoint));
  TestExtension<1>* extension_1 = QueryExtension<TestExtension<1>>(
      static_cast<EventEngine::Endpoint*>(&endpoint));
  TestExtension<2>* extension_2 = QueryExtension<TestExtension<2>>(
      static_cast<EventEngine::Endpoint*>(&endpoint));
=======
  TestExtension<0>* extension_0 = QueryExtension<TestExtension<0>>(&endpoint);
  TestExtension<1>* extension_1 = QueryExtension<TestExtension<1>>(&endpoint);
  TestExtension<2>* extension_2 = QueryExtension<TestExtension<2>>(&endpoint);
>>>>>>> 078d94981ac22dc30fc6cd7ee4accc62602c86d8

  EXPECT_NE(extension_0, nullptr);
  EXPECT_NE(extension_1, nullptr);
  EXPECT_NE(extension_2, nullptr);

  EXPECT_EQ(extension_0->GetValue(), 0);
  EXPECT_EQ(extension_1->GetValue(), 1);
  EXPECT_EQ(extension_2->GetValue(), 2);
}
}  // namespace

<<<<<<< HEAD
}  // namespace grpc_event_engine::experimental
=======
}  // namespace experimental
}  // namespace grpc_event_engine
>>>>>>> 078d94981ac22dc30fc6cd7ee4accc62602c86d8

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
<<<<<<< HEAD
}
=======
}
>>>>>>> 078d94981ac22dc30fc6cd7ee4accc62602c86d8
