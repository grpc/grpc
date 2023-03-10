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

#include <errno.h>
#include <sys/socket.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/strerror.h"
#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::PosixSocketWrapper;

// There is a special code path in create_socket to log errors upon EMFILE.
// Goal of this test is just to exercise that code path and also make sure
// it doesn't mess up "errno", so that we get the right error message.
TEST(LogTooManyOpenFilesTest, MainTest) {
  const auto mock_socket_factory = [](int, int, int) {
    errno = EMFILE;
    return -1;
  };
  auto addr = grpc_event_engine::experimental::URIToResolvedAddress(
      "ipv4:127.0.0.1:80");
  ASSERT_TRUE(addr.ok());
  PosixSocketWrapper::DSMode dsmode;
  absl::StatusOr<PosixSocketWrapper> result =
      PosixSocketWrapper::CreateDualStackSocket(mock_socket_factory, *addr,
                                                SOCK_STREAM, AF_INET, dsmode);
  EXPECT_FALSE(result.ok());
  std::string emfile_message = grpc_core::StrError(EMFILE);
  EXPECT_THAT(result.status().message(), ::testing::HasSubstr(emfile_message));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
