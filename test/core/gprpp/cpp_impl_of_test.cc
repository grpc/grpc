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

#include "src/core/lib/gprpp/cpp_impl_of.h"

#include <memory>

#include <gtest/gtest.h>

typedef struct grpc_foo grpc_foo;

namespace grpc_core {
namespace {
class Foo : public CppImplOf<Foo, grpc_foo> {};
}  // namespace

TEST(CppImplOfTest, CreateDestroy) { delete Foo::FromC((new Foo())->c_ptr()); }

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
