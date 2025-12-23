/*
 *
 * Copyright 2025 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "absl/log/log.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "test/core/test_util/test_config.h"

#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>

TEST(UdsSocketTest, UnixSockaddrPopulateAndLen) {
  grpc_resolved_address resolved_addr;
  const char* kPath = "/tmp/grpc_test_socket.sock";

  // Populate the address using the core utility
  absl::Status status = grpc_core::UnixSockaddrPopulate(kPath, &resolved_addr);
  ASSERT_TRUE(status.ok()) << status;

  struct sockaddr_un* un =
      reinterpret_cast<struct sockaddr_un*>(resolved_addr.addr);

  // Verify Family
  EXPECT_EQ(un->sun_family, AF_UNIX);

  // Verify Path
  EXPECT_STREQ(un->sun_path, kPath);

  // Verify Length (Platform Specific)
#ifdef __APPLE__
  // On Apple platforms, sun_len must be set correctly.
  // This is expected to FAIL until the core library is fixed.
  EXPECT_GT(un->sun_len, 0);

  // The logic inside grpc_core::UnixSockaddrPopulate should match:
  // len = sizeof(sockaddr_un) - (sizeof(sun_path) - path_len)
  // where path_len includes the null terminator.
  size_t path_len = strlen(kPath) + 1;
  size_t expected_len =
      sizeof(struct sockaddr_un) - (sizeof(un->sun_path) - path_len);

  EXPECT_EQ(un->sun_len, expected_len)
      << "Expected sun_len to be calculated based on path length";
  LOG(INFO) << "Verified sun_len: " << (int)un->sun_len;
#else
  LOG(INFO) << "Skipping sun_len check on non-Apple platform";
#endif
}

TEST(UdsSocketTest, MaxPathLength) {
  grpc_resolved_address resolved_addr;
  struct sockaddr_un un_struct;
  // maxlen is usually sizeof(sun_path) - 1 (103 or 107 depending on platform)
  size_t maxlen = sizeof(un_struct.sun_path) - 1;
  std::string kPath(maxlen, 'a');

  absl::Status status = grpc_core::UnixSockaddrPopulate(kPath, &resolved_addr);
  ASSERT_TRUE(status.ok()) << status;

  struct sockaddr_un* un =
      reinterpret_cast<struct sockaddr_un*>(resolved_addr.addr);
  EXPECT_EQ(strlen(un->sun_path), maxlen);
}

TEST(UdsSocketTest, PathTooLong) {
  grpc_resolved_address resolved_addr;
  struct sockaddr_un un_struct;
  size_t maxlen =
      sizeof(un_struct.sun_path);  // One char too many (maxlen is size - 1)
  std::string kPath(maxlen, 'a');

  absl::Status status = grpc_core::UnixSockaddrPopulate(kPath, &resolved_addr);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(),
            absl::StatusCode::kUnknown);  // GRPC_ERROR_CREATE defaults to
                                          // unknown/internal usually, or
                                          // generic error
}

#endif  // GRPC_HAVE_UNIX_SOCKET

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
