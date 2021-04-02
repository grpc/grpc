/*
 *
 * Copyright 2020 the gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpcpp/test/mock_stream.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include "src/proto/grpc/testing/echo.grpc.pb.h"

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;

TEST(MockStreamTest, Basic) {
  auto cr = absl::make_unique<grpc::testing::MockClientReader<EchoResponse>>();
  ASSERT_NE(cr, nullptr);

  auto cw = absl::make_unique<grpc::testing::MockClientWriter<EchoResponse>>();
  ASSERT_NE(cw, nullptr);

  auto crw = absl::make_unique<
      grpc::testing::MockClientReaderWriter<EchoResponse, EchoResponse>>();
  ASSERT_NE(crw, nullptr);

  auto carr = absl::make_unique<
      grpc::testing::MockClientAsyncResponseReader<EchoResponse>>();
  ASSERT_NE(carr, nullptr);

  auto car =
      absl::make_unique<grpc::testing::MockClientAsyncReader<EchoResponse>>();
  ASSERT_NE(car, nullptr);

  auto caw =
      absl::make_unique<grpc::testing::MockClientAsyncWriter<EchoResponse>>();
  ASSERT_NE(caw, nullptr);

  auto carw = absl::make_unique<
      grpc::testing::MockClientAsyncReaderWriter<EchoRequest, EchoResponse>>();
  ASSERT_NE(carw, nullptr);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
