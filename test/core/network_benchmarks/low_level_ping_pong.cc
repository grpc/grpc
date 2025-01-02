//
//
// Copyright 2015 gRPC authors.
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

//
// Basic I/O ping-pong benchmarks.

// The goal here is to establish lower bounds on how fast the stack could get by
// measuring the cost of using various I/O strategies to do a basic
// request-response loop.
//

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/epoll.h>
#endif
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>
#include <sys/socket.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/util/strerror.h"
#include "src/core/util/thd.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/cmdline.h"
#include "test/core/test_util/histogram.h"

typedef struct fd_pair {
  int read_fd;
  int write_fd;
} fd_pair;

typedef struct thread_args {
  fd_pair fds;
  size_t msg_size;
  int (*read_bytes)(struct thread_args* args, char* buf);
  int (*write_bytes)(struct thread_args* args, char* buf);
  int (*setup)(struct thread_args* args);
  int epoll_fd;
  const char* strategy_name;
} thread_args;

//
// Read strategies

// There are a number of read strategies, each of which has a blocking and
// non-blocking version.
//

// Basic call to read()
static int read_bytes(int fd, char* buf, size_t read_size, int spin) {
  size_t bytes_read = 0;
  ssize_t err;
  do {
    err = read(fd, buf + bytes_read, read_size - bytes_read);
    if (err < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        if (errno == EAGAIN && spin == 1) {
          continue;
        }
        LOG(ERROR) << "Read failed: " << grpc_core::StrError(errno);
        return -1;
      }
    } else {
      bytes_read += static_cast<size_t>(err);
    }
  } while (bytes_read < read_size);
  return 0;
}

static int blocking_read_bytes(thread_args* args, char* buf) {
  return read_bytes(args->fds.read_fd, buf, args->msg_size, 0);
}

static int spin_read_bytes(thread_args* args, char* buf) {
  return read_bytes(args->fds.read_fd, buf, args->msg_size, 1);
}

// Call poll() to monitor a non-blocking fd
static int poll_read_bytes(int fd, char* buf, size_t read_size, int spin) {
  struct pollfd pfd;
  size_t bytes_read = 0;
  int err;
  ssize_t err2;

  pfd.fd = fd;
  pfd.events = POLLIN;
  do {
    err = poll(&pfd, 1, spin ? 0 : -1);
    if (err < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        LOG(ERROR) << "Poll failed: " << grpc_core::StrError(errno);
        return -1;
      }
    }
    if (err == 0 && spin) continue;
    CHECK_EQ(err, 1);
    CHECK(pfd.revents == POLLIN);
    do {
      err2 = read(fd, buf + bytes_read, read_size - bytes_read);
    } while (err2 < 0 && errno == EINTR);
    if (err2 < 0 && errno != EAGAIN) {
      LOG(ERROR) << "Read failed: " << grpc_core::StrError(errno);
      return -1;
    }
    bytes_read += static_cast<size_t>(err2);
  } while (bytes_read < read_size);
  return 0;
}

static int poll_read_bytes_blocking(struct thread_args* args, char* buf) {
  return poll_read_bytes(args->fds.read_fd, buf, args->msg_size, 0);
}

static int poll_read_bytes_spin(struct thread_args* args, char* buf) {
  return poll_read_bytes(args->fds.read_fd, buf, args->msg_size, 1);
}

#ifdef __linux__
// Call epoll_wait() to monitor a non-blocking fd
static int epoll_read_bytes(struct thread_args* args, char* buf, int spin) {
  struct epoll_event ev;
  size_t bytes_read = 0;
  int err;
  ssize_t err2;
  size_t read_size = args->msg_size;

  do {
    err = epoll_wait(args->epoll_fd, &ev, 1, spin ? 0 : -1);
    if (err < 0) {
      if (errno == EINTR) continue;
      LOG(ERROR) << "epoll_wait failed: " << grpc_core::StrError(errno);
      return -1;
    }
    if (err == 0 && spin) continue;
    CHECK_EQ(err, 1);
    CHECK(ev.events & EPOLLIN);
    CHECK(ev.data.fd == args->fds.read_fd);
    do {
      do {
        err2 =
            read(args->fds.read_fd, buf + bytes_read, read_size - bytes_read);
      } while (err2 < 0 && errno == EINTR);
      if (errno == EAGAIN) break;
      bytes_read += static_cast<size_t>(err2);
      // TODO(klempner): This should really be doing an extra call after we are
      // done to ensure we see an EAGAIN
    } while (bytes_read < read_size);
  } while (bytes_read < read_size);
  CHECK(bytes_read == read_size);
  return 0;
}

static int epoll_read_bytes_blocking(struct thread_args* args, char* buf) {
  return epoll_read_bytes(args, buf, 0);
}

static int epoll_read_bytes_spin(struct thread_args* args, char* buf) {
  return epoll_read_bytes(args, buf, 1);
}
#endif  // __linux__

// Write out bytes.
// At this point we only have one strategy, since in the common case these
// writes go directly out to the kernel.
//
static int blocking_write_bytes(struct thread_args* args, char* buf) {
  size_t bytes_written = 0;
  ssize_t err;
  size_t write_size = args->msg_size;
  do {
    err = write(args->fds.write_fd, buf + bytes_written,
                write_size - bytes_written);
    if (err < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        LOG(ERROR) << "Read failed: " << grpc_core::StrError(errno);
        return -1;
      }
    } else {
      bytes_written += static_cast<size_t>(err);
    }
  } while (bytes_written < write_size);
  return 0;
}

//
// Initialization code

// These are called at the beginning of the client and server thread, depending
// on the scenario we're using.
//
static int set_socket_nonblocking(thread_args* args) {
  if (!GRPC_LOG_IF_ERROR("Unable to set read socket nonblocking",
                         grpc_set_socket_nonblocking(args->fds.read_fd, 1))) {
    return -1;
  }
  if (!GRPC_LOG_IF_ERROR("Unable to set write socket nonblocking",
                         grpc_set_socket_nonblocking(args->fds.write_fd, 1))) {
    return -1;
  }
  return 0;
}

static int do_nothing(thread_args* /*args*/) { return 0; }

#ifdef __linux__
// Special case for epoll, where we need to create the fd ahead of time.
static int epoll_setup(thread_args* args) {
  int epoll_fd;
  struct epoll_event ev;
  set_socket_nonblocking(args);
  epoll_fd = epoll_create(1);
  if (epoll_fd < 0) {
    LOG(ERROR) << "epoll_create: " << grpc_core::StrError(errno);
    return -1;
  }

  args->epoll_fd = epoll_fd;

  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = args->fds.read_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, args->fds.read_fd, &ev) < 0) {
    LOG(ERROR) << "epoll_ctl: " << grpc_core::StrError(errno);
  }
  return 0;
}
#endif

static void server_thread(thread_args* args) {
  char* buf = static_cast<char*>(gpr_malloc(args->msg_size));
  if (args->setup(args) < 0) {
    LOG(ERROR) << "Setup failed";
  }
  for (;;) {
    if (args->read_bytes(args, buf) < 0) {
      LOG(ERROR) << "Server read failed";
      gpr_free(buf);
      return;
    }
    if (args->write_bytes(args, buf) < 0) {
      LOG(ERROR) << "Server write failed";
      gpr_free(buf);
      return;
    }
  }
}

static void server_thread_wrap(void* arg) {
  thread_args* args = static_cast<thread_args*>(arg);
  server_thread(args);
}

static void print_histogram(grpc_histogram* histogram) {
  // TODO(klempner): Print more detailed information, such as detailed histogram
  // buckets
  LOG(INFO) << "latency (50/95/99/99.9): "
            << grpc_histogram_percentile(histogram, 50) << "/"
            << grpc_histogram_percentile(histogram, 95) << "/"
            << grpc_histogram_percentile(histogram, 99) << "/"
            << grpc_histogram_percentile(histogram, 99.9);
}

static double now(void) {
  gpr_timespec tv = gpr_now(GPR_CLOCK_REALTIME);
  return (1e9 * static_cast<double>(tv.tv_sec)) +
         static_cast<double>(tv.tv_nsec);
}

static void client_thread(thread_args* args) {
  char* buf = static_cast<char*>(gpr_malloc(args->msg_size * sizeof(char)));
  memset(buf, 0, args->msg_size * sizeof(char));
  grpc_histogram* histogram = grpc_histogram_create(0.01, 60e9);
  double start_time;
  double end_time;
  double interval;
  const int kNumIters = 100000;
  int i;

  if (args->setup(args) < 0) {
    LOG(ERROR) << "Setup failed";
  }
  for (i = 0; i < kNumIters; ++i) {
    start_time = now();
    if (args->write_bytes(args, buf) < 0) {
      LOG(ERROR) << "Client write failed";
      goto error;
    }
    if (args->read_bytes(args, buf) < 0) {
      LOG(ERROR) << "Client read failed";
      goto error;
    }
    end_time = now();
    if (i > kNumIters / 2) {
      interval = end_time - start_time;
      grpc_histogram_add(histogram, interval);
    }
  }
  print_histogram(histogram);
error:
  gpr_free(buf);
  grpc_histogram_destroy(histogram);
}

// This roughly matches tcp_server's create_listening_socket
static int create_listening_socket(struct sockaddr* port, socklen_t len) {
  int fd = socket(port->sa_family, SOCK_STREAM, 0);
  if (fd < 0) {
    LOG(ERROR) << "Unable to create socket: " << grpc_core::StrError(errno);
    goto error;
  }

  if (!GRPC_LOG_IF_ERROR("Failed to set listening socket cloexec",
                         grpc_set_socket_cloexec(fd, 1))) {
    goto error;
  }
  if (!GRPC_LOG_IF_ERROR("Failed to set listening socket low latency",
                         grpc_set_socket_low_latency(fd, 1))) {
    goto error;
  }
  if (!GRPC_LOG_IF_ERROR("Failed to set listening socket reuse addr",
                         grpc_set_socket_reuse_addr(fd, 1))) {
    goto error;
  }

  if (bind(fd, port, len) < 0) {
    LOG(ERROR) << "bind: " << grpc_core::StrError(errno);
    goto error;
  }

  if (listen(fd, 1) < 0) {
    LOG(ERROR) << "listen: " << grpc_core::StrError(errno);
    goto error;
  }

  if (getsockname(fd, port, &len) < 0) {
    LOG(ERROR) << "getsockname: " << grpc_core::StrError(errno);
    goto error;
  }

  return fd;

error:
  if (fd >= 0) {
    close(fd);
  }
  return -1;
}

static int connect_client(struct sockaddr* addr, socklen_t len) {
  int fd = socket(addr->sa_family, SOCK_STREAM, 0);
  int err;
  if (fd < 0) {
    LOG(ERROR) << "Unable to create socket: " << grpc_core::StrError(errno);
    goto error;
  }

  if (!GRPC_LOG_IF_ERROR("Failed to set connecting socket cloexec",
                         grpc_set_socket_cloexec(fd, 1))) {
    goto error;
  }
  if (!GRPC_LOG_IF_ERROR("Failed to set connecting socket low latency",
                         grpc_set_socket_low_latency(fd, 1))) {
    goto error;
  }

  do {
    err = connect(fd, addr, len);
  } while (err < 0 && errno == EINTR);

  if (err < 0) {
    LOG(ERROR) << "connect error: " << grpc_core::StrError(errno);
    goto error;
  }
  return fd;

error:
  if (fd >= 0) {
    close(fd);
  }
  return -1;
}

static int accept_server(int listen_fd) {
  int fd = accept(listen_fd, nullptr, nullptr);
  if (fd < 0) {
    LOG(ERROR) << "Accept failed: " << grpc_core::StrError(errno);
    return -1;
  }
  return fd;
}

static int create_sockets_tcp(fd_pair* client_fds, fd_pair* server_fds) {
  int listen_fd = -1;
  int client_fd = -1;
  int server_fd = -1;

  struct sockaddr_in port;
  struct sockaddr* sa_port = reinterpret_cast<struct sockaddr*>(&port);

  port.sin_family = AF_INET;
  port.sin_port = 0;
  port.sin_addr.s_addr = INADDR_ANY;

  listen_fd = create_listening_socket(sa_port, sizeof(port));
  if (listen_fd == -1) {
    LOG(ERROR) << "Listen failed";
    goto error;
  }

  client_fd = connect_client(sa_port, sizeof(port));
  if (client_fd == -1) {
    LOG(ERROR) << "Connect failed";
    goto error;
  }

  server_fd = accept_server(listen_fd);
  if (server_fd == -1) {
    LOG(ERROR) << "Accept failed";
    goto error;
  }

  client_fds->read_fd = client_fd;
  client_fds->write_fd = client_fd;
  server_fds->read_fd = server_fd;
  server_fds->write_fd = server_fd;
  close(listen_fd);
  return 0;

error:
  if (listen_fd != -1) {
    close(listen_fd);
  }
  if (client_fd != -1) {
    close(client_fd);
  }
  if (server_fd != -1) {
    close(server_fd);
  }
  return -1;
}

static int create_sockets_socketpair(fd_pair* client_fds, fd_pair* server_fds) {
  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
    LOG(ERROR) << "socketpair: " << grpc_core::StrError(errno);
    return -1;
  }

  client_fds->read_fd = fds[0];
  client_fds->write_fd = fds[0];
  server_fds->read_fd = fds[1];
  server_fds->write_fd = fds[1];
  return 0;
}

static int create_sockets_pipe(fd_pair* client_fds, fd_pair* server_fds) {
  int cfds[2];
  int sfds[2];
  if (pipe(cfds) < 0) {
    LOG(ERROR) << "pipe: " << grpc_core::StrError(errno);
    return -1;
  }

  if (pipe(sfds) < 0) {
    LOG(ERROR) << "pipe: " << grpc_core::StrError(errno);
    return -1;
  }

  client_fds->read_fd = cfds[0];
  client_fds->write_fd = cfds[1];
  server_fds->read_fd = sfds[0];
  server_fds->write_fd = sfds[1];
  return 0;
}

static const char* read_strategy_usage =
    "Strategy for doing reads, which is one of:\n"
    "  blocking: blocking read calls\n"
    "  same_thread_poll: poll() call on same thread \n"
#ifdef __linux__
    "  same_thread_epoll: epoll_wait() on same thread \n"
#endif
    "  spin_read: spinning non-blocking read() calls \n"
    "  spin_poll: spinning 0 timeout poll() calls \n"
#ifdef __linux__
    "  spin_epoll: spinning 0 timeout epoll_wait() calls \n"
#endif
    "";

static const char* socket_type_usage =
    "Type of socket used, one of:\n"
    "  tcp: fds are endpoints of a TCP connection\n"
    "  socketpair: fds come from socketpair()\n"
    "  pipe: fds come from pipe()\n";

void print_usage(char* argv0) {
  fprintf(stderr, "%s usage:\n\n", argv0);
  fprintf(stderr, "%s read_strategy socket_type msg_size\n\n", argv0);
  fprintf(stderr, "where read_strategy is one of:\n");
  fprintf(stderr, "  blocking: blocking read calls\n");
  fprintf(stderr, "  same_thread_poll: poll() call on same thread \n");
#ifdef __linux__
  fprintf(stderr, "  same_thread_epoll: epoll_wait() on same thread \n");
#endif
  fprintf(stderr, "  spin_read: spinning non-blocking read() calls \n");
  fprintf(stderr, "  spin_poll: spinning 0 timeout poll() calls \n");
#ifdef __linux__
  fprintf(stderr, "  spin_epoll: spinning 0 timeout epoll_wait() calls \n");
#endif
  fprintf(stderr, "and socket_type is one of:\n");
  fprintf(stderr, "  tcp: fds are endpoints of a TCP connection\n");
  fprintf(stderr, "  socketpair: fds come from socketpair()\n");
  fprintf(stderr, "  pipe: fds come from pipe()\n");
  fflush(stderr);
}

typedef struct test_strategy {
  const char* name;
  int (*read_strategy)(struct thread_args* args, char* buf);
  int (*setup)(struct thread_args* args);
} test_strategy;

static test_strategy test_strategies[] = {
    {"blocking", blocking_read_bytes, do_nothing},
    {"same_thread_poll", poll_read_bytes_blocking, set_socket_nonblocking},
#ifdef __linux__
    {"same_thread_epoll", epoll_read_bytes_blocking, epoll_setup},
    {"spin_epoll", epoll_read_bytes_spin, epoll_setup},
#endif  // __linux__
    {"spin_read", spin_read_bytes, set_socket_nonblocking},
    {"spin_poll", poll_read_bytes_spin, set_socket_nonblocking}};

static const char* socket_types[] = {"tcp", "socketpair", "pipe"};

int create_socket(const char* socket_type, fd_pair* client_fds,
                  fd_pair* server_fds) {
  if (strcmp(socket_type, "tcp") == 0) {
    create_sockets_tcp(client_fds, server_fds);
  } else if (strcmp(socket_type, "socketpair") == 0) {
    create_sockets_socketpair(client_fds, server_fds);
  } else if (strcmp(socket_type, "pipe") == 0) {
    create_sockets_pipe(client_fds, server_fds);
  } else {
    fprintf(stderr, "Invalid socket type %s\n", socket_type);
    fflush(stderr);
    return -1;
  }
  return 0;
}

static int run_benchmark(const char* socket_type, thread_args* client_args,
                         thread_args* server_args) {
  int rv = 0;

  rv = create_socket(socket_type, &client_args->fds, &server_args->fds);
  if (rv < 0) {
    return rv;
  }

  LOG(INFO) << "Starting test " << client_args->strategy_name << " "
            << socket_type << " " << client_args->msg_size;

  grpc_core::Thread server("server_thread", server_thread_wrap, server_args);
  server.Start();
  client_thread(client_args);
  server.Join();

  return 0;
}

static int run_all_benchmarks(size_t msg_size) {
  int error = 0;
  size_t i;
  for (i = 0; i < GPR_ARRAY_SIZE(test_strategies); ++i) {
    test_strategy* strategy = &test_strategies[i];
    size_t j;
    for (j = 0; j < GPR_ARRAY_SIZE(socket_types); ++j) {
      thread_args* client_args =
          static_cast<thread_args*>(gpr_malloc(sizeof(thread_args)));
      thread_args* server_args =
          static_cast<thread_args*>(gpr_malloc(sizeof(thread_args)));
      const char* socket_type = socket_types[j];

      client_args->read_bytes = strategy->read_strategy;
      client_args->write_bytes = blocking_write_bytes;
      client_args->setup = strategy->setup;
      client_args->msg_size = msg_size;
      client_args->strategy_name = strategy->name;
      server_args->read_bytes = strategy->read_strategy;
      server_args->write_bytes = blocking_write_bytes;
      server_args->setup = strategy->setup;
      server_args->msg_size = msg_size;
      server_args->strategy_name = strategy->name;
      error = run_benchmark(socket_type, client_args, server_args);
      if (error < 0) {
        return error;
      }
    }
  }
  return error;
}

int main(int argc, char** argv) {
  thread_args* client_args =
      static_cast<thread_args*>(gpr_malloc(sizeof(thread_args)));
  thread_args* server_args =
      static_cast<thread_args*>(gpr_malloc(sizeof(thread_args)));
  int msg_size = -1;
  const char* read_strategy = nullptr;
  const char* socket_type = nullptr;
  size_t i;
  const test_strategy* strategy = nullptr;
  int error = 0;

  gpr_cmdline* cmdline =
      gpr_cmdline_create("low_level_ping_pong network benchmarking tool");

  gpr_cmdline_add_int(cmdline, "msg_size", "Size of sent messages", &msg_size);
  gpr_cmdline_add_string(cmdline, "read_strategy", read_strategy_usage,
                         &read_strategy);
  gpr_cmdline_add_string(cmdline, "socket_type", socket_type_usage,
                         &socket_type);

  gpr_cmdline_parse(cmdline, argc, argv);

  if (msg_size == -1) {
    msg_size = 50;
  }

  if (read_strategy == nullptr) {
    LOG(INFO) << "No strategy specified, running all benchmarks";
    return run_all_benchmarks(static_cast<size_t>(msg_size));
  }

  if (socket_type == nullptr) {
    socket_type = "tcp";
  }
  if (msg_size <= 0) {
    fprintf(stderr, "msg_size must be > 0\n");
    fflush(stderr);
    print_usage(argv[0]);
    return -1;
  }

  for (i = 0; i < GPR_ARRAY_SIZE(test_strategies); ++i) {
    if (strcmp(test_strategies[i].name, read_strategy) == 0) {
      strategy = &test_strategies[i];
    }
  }
  if (strategy == nullptr) {
    fprintf(stderr, "Invalid read strategy %s\n", read_strategy);
    fflush(stderr);
    return -1;
  }

  client_args->read_bytes = strategy->read_strategy;
  client_args->write_bytes = blocking_write_bytes;
  client_args->setup = strategy->setup;
  client_args->msg_size = static_cast<size_t>(msg_size);
  client_args->strategy_name = read_strategy;
  server_args->read_bytes = strategy->read_strategy;
  server_args->write_bytes = blocking_write_bytes;
  server_args->setup = strategy->setup;
  server_args->msg_size = static_cast<size_t>(msg_size);
  server_args->strategy_name = read_strategy;

  error = run_benchmark(socket_type, client_args, server_args);

  gpr_cmdline_destroy(cmdline);
  return error;
}
