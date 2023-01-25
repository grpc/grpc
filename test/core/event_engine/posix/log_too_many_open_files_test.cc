#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"

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
  grpc_event_engine::experimental::PosixSocketWrapper::DSMode dsmode;
  absl::StatusOr<grpc_event_engine::experimental::PosixSocketWrapper> result =
      grpc_event_engine::experimental::PosixSocketWrapper::
          CreateDualStackSocket(mock_socket_factory, *addr, SOCK_STREAM,
                                AF_INET, dsmode);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(),
              ::testing::HasSubstr("Too many open files"));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
