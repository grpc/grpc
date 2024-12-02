#include "src/core/lib/event_engine/posix_engine/posix_system_api.h"

#include <arpa/inet.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include "gmock/gmock.h"
#include "test/core/test_util/port.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

std::string ErrorString() {
  return absl::StrFormat("(%d) %s", errno, strerror(errno));
}

MATCHER(IsPosixSuccess, "") {
  if ((arg == 0)) {
    return true;
  }
  *result_listener << absl::StrFormat("(%d) %s", errno, strerror(errno));
  return false;
}

}  // namespace

TEST(PosixSystemApiTest, EndpointSurvivesGeneration) {
  int port = grpc_pick_unused_port_or_die();
  SystemApi server_api;
  SystemApi client_api;

  FileDescriptor server =
      server_api.Socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_TRUE(server.ready()) << ErrorString();
  sockaddr_in addr{0};
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;
  sockaddr* sa = reinterpret_cast<sockaddr*>(&addr);
  int result = server_api.Bind(server, sa, sizeof(addr));
  ASSERT_EQ(server_api.Listen(server, 3), 0) << ErrorString();
  ASSERT_EQ(result, 0) << ErrorString();
  FileDescriptor client =
      client_api.Socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_TRUE(server.ready()) << ErrorString();
  result = client_api.Connect(client, sa, sizeof(addr));
  ASSERT_LT(result, 0);
  ASSERT_EQ(errno, EINPROGRESS) << ErrorString();
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  FileDescriptor server_end =
      server_api.Accept(server, reinterpret_cast<sockaddr*>(&ss), &slen);
  ASSERT_TRUE(server_end.ready()) << ErrorString();
  result = client_api.Connect(client, sa, sizeof(addr));
  ASSERT_EQ(result, 0);
  std::array<uint8_t, 3> buf = {0x20, 0x30, 0x30};
  ASSERT_EQ(client_api.Write(client, buf.data(), buf.size()), buf.size());
  std::array<uint8_t, 20> rcv;
  rcv.fill(0);
  ASSERT_EQ(server_api.Read(server_end, rcv.data(), rcv.size()), buf.size());
  EXPECT_THAT(absl::MakeSpan(rcv).first(buf.size()),
              ::testing::ElementsAreArray(buf));
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
