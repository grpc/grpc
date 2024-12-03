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
#include "gtest/gtest.h"
#include "test/core/test_util/port.h"

namespace grpc_event_engine {
namespace experimental {

namespace {

std::string ErrorString() {
  return absl::StrFormat("(%d) %s", errno, strerror(errno));
}

MATCHER(FDReady, "file descriptor is ready") { return ((arg.ready())); }

MATCHER_P(IsOkWith, value, "") {
  if ((arg.ok())) {
    return testing::ExplainMatchResult(value, arg.value(), result_listener);
  } else {
    *result_listener << absl::StrFormat(
        "failed with (%d) %s", arg.status().code(), arg.status().message());
    return false;
  }
}

sockaddr_in SockAddr(int port) {
  sockaddr_in addr{0};
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;
  return addr;
}

absl::StatusOr<FileDescriptor> Listen(SystemApi& system_api, int port) {
  FileDescriptor server =
      system_api.Socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (!server.ready()) {
    return absl::ErrnoToStatus(errno, "Failed to create a socket");
  }
  sockaddr_in addr = SockAddr(port);
  sockaddr* sa = reinterpret_cast<sockaddr*>(&addr);
  if (system_api.Bind(server, sa, sizeof(addr)) < 0) {
    return absl::ErrnoToStatus(errno, "Bind to an address failed");
  }
  if (system_api.Listen(server, 1) < 0) {
    return absl::ErrnoToStatus(errno, "Listen failed");
  };
  return server;
}

absl::StatusOr<FileDescriptor> Accept(SystemApi& system_api,
                                      const FileDescriptor& fd) {
  struct sockaddr_storage ss;
  socklen_t slen = sizeof(ss);
  FileDescriptor server_end =
      system_api.Accept(fd, reinterpret_cast<sockaddr*>(&ss), &slen);
  if (!server_end.ready()) {
    return absl::ErrnoToStatus(errno, "Accept failed");
  }
  return server_end;
}

struct ClientAndServer {
  FileDescriptor client;
  FileDescriptor server;
};

absl::StatusOr<ClientAndServer> EstablishConnection(
    SystemApi& server_system_api, SystemApi& client_system_api,
    const FileDescriptor& server, int port) {
  FileDescriptor client =
      client_system_api.Socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (!client.ready()) {
    return absl::ErrnoToStatus(errno, "Unable to create a client socket");
  }
  sockaddr_in addr = SockAddr(port);
  int result = client_system_api.Connect(
      client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (result == 0 || errno != EINPROGRESS) {
    return absl::ErrnoToStatus(errno, "Connect is not EINPROGRESS");
  }
  auto server_end = Accept(server_system_api, server);
  if (!server_end.ok()) {
    return std::move(server_end).status();
  }
  result = client_system_api.Connect(client, reinterpret_cast<sockaddr*>(&addr),
                                     sizeof(addr));
  if (result < 0) {
    return absl::ErrnoToStatus(errno, "Second connect failed");
  }
  return ClientAndServer{client, std::move(server_end).value()};
}

}  // namespace

TEST(PosixSystemApiTest, PosixLevel) {
  SystemApi server_api;
  SystemApi client_api;

  int port = grpc_pick_unused_port_or_die();

  auto server = Listen(server_api, port);
  ASSERT_THAT(server, IsOkWith(FDReady()));
  ASSERT_EQ(errno, EINPROGRESS) << ErrorString();
  auto server_client =
      EstablishConnection(server_api, client_api, *server, port);
  ASSERT_TRUE(server_client.ok()) << server_client.status();
  // Send from client to server
  std::array<uint8_t, 3> buf = {0x20, 0x30, 0x30};
  ASSERT_THAT(client_api.Write(server_client->client, buf.data(), buf.size()),
              IsOkWith(buf.size()));
  std::array<uint8_t, 20> rcv;
  rcv.fill(0);
  ASSERT_EQ(server_api.Read(server_client->server, rcv.data(), rcv.size()),
            buf.size());
  EXPECT_THAT(absl::MakeSpan(rcv).first(buf.size()),
              ::testing::ElementsAreArray(buf));
  // Client "forks"
  client_api.AdvanceGeneration();
  ASSERT_EQ(client_api.Write(server_client->client, buf.data(), buf.size())
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  // Send using the new connection
  server_client = EstablishConnection(server_api, client_api, *server, port);
  ASSERT_TRUE(server_client.ok()) << server_client.status();
  ASSERT_THAT(client_api.Write(server_client->client, buf.data(), buf.size()),
              IsOkWith(buf.size()));
  // Make sure previous run does not leak here
  rcv.fill(0);
  ASSERT_EQ(server_api.Read(server_client->server, rcv.data(), rcv.size()),
            buf.size());
  EXPECT_THAT(absl::MakeSpan(rcv).first(buf.size()),
              ::testing::ElementsAreArray(buf));
}

TEST(PosixSystemApiTest, EventEndpointLevel) {
  SystemApi server_api;
  SystemApi client_api;
}

}  // namespace experimental
}  // namespace grpc_event_engine

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
