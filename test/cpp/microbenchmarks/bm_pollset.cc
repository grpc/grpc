/*
 *
 * Copyright 2017 gRPC authors.
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

/* Test out pollset latencies */

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

#include <string.h>

#ifdef GRPC_LINUX_MULTIPOLL_WITH_EPOLL
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

static void shutdown_ps(void* ps, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(ps));
}

static void BM_CreateDestroyPollset(benchmark::State& state) {
  TrackCounters track_counters;
  size_t ps_sz = grpc_pollset_size();
  grpc_pollset* ps = static_cast<grpc_pollset*>(gpr_malloc(ps_sz));
  gpr_mu* mu;
  grpc_core::ExecCtx exec_ctx;
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  for (auto _ : state) {
    memset(ps, 0, ps_sz);
    grpc_pollset_init(ps, &mu);
    gpr_mu_lock(mu);
    grpc_pollset_shutdown(ps, &shutdown_ps_closure);
    gpr_mu_unlock(mu);
    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(ps);
  track_counters.Finish(state);
}
BENCHMARK(BM_CreateDestroyPollset);

#ifdef GRPC_LINUX_MULTIPOLL_WITH_EPOLL
static void BM_PollEmptyPollset_SpeedOfLight(benchmark::State& state) {
  // equivalent to BM_PollEmptyPollset, but just use the OS primitives to guage
  // what the speed of light would be if we abstracted perfectly
  TrackCounters track_counters;
  int epfd = epoll_create1(0);
  GPR_ASSERT(epfd != -1);
  size_t nev = state.range(0);
  size_t nfd = state.range(1);
  epoll_event* ev = new epoll_event[nev];
  std::vector<int> fds;
  for (size_t i = 0; i < nfd; i++) {
    fds.push_back(eventfd(0, 0));
    epoll_event ev;
    ev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fds.back(), &ev);
  }
  for (auto _ : state) {
    epoll_wait(epfd, ev, nev, 0);
  }
  for (auto fd : fds) {
    close(fd);
  }
  close(epfd);
  delete[] ev;
  track_counters.Finish(state);
}
BENCHMARK(BM_PollEmptyPollset_SpeedOfLight)
    ->Args({1, 0})
    ->Args({1, 1})
    ->Args({1, 10})
    ->Args({1, 100})
    ->Args({1, 1000})
    ->Args({1, 10000})
    ->Args({1, 100000})
    ->Args({10, 1})
    ->Args({100, 1})
    ->Args({1000, 1});
#endif

static void BM_PollEmptyPollset(benchmark::State& state) {
  TrackCounters track_counters;
  size_t ps_sz = grpc_pollset_size();
  grpc_pollset* ps = static_cast<grpc_pollset*>(gpr_zalloc(ps_sz));
  gpr_mu* mu;
  grpc_pollset_init(ps, &mu);
  grpc_core::ExecCtx exec_ctx;
  gpr_mu_lock(mu);
  for (auto _ : state) {
    GRPC_ERROR_UNREF(grpc_pollset_work(ps, nullptr, 0));
  }
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(ps, &shutdown_ps_closure);
  gpr_mu_unlock(mu);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(ps);
  track_counters.Finish(state);
}
BENCHMARK(BM_PollEmptyPollset);

static void BM_PollAddFd(benchmark::State& state) {
  TrackCounters track_counters;
  size_t ps_sz = grpc_pollset_size();
  grpc_pollset* ps = static_cast<grpc_pollset*>(gpr_zalloc(ps_sz));
  gpr_mu* mu;
  grpc_pollset_init(ps, &mu);
  grpc_core::ExecCtx exec_ctx;
  grpc_wakeup_fd wakeup_fd;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("wakeup_fd_init", grpc_wakeup_fd_init(&wakeup_fd)));
  grpc_fd* fd = grpc_fd_create(wakeup_fd.read_fd, "xxx", false);
  for (auto _ : state) {
    grpc_pollset_add_fd(ps, fd);
    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_fd_orphan(fd, nullptr, nullptr, "xxx");
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  gpr_mu_lock(mu);
  grpc_pollset_shutdown(ps, &shutdown_ps_closure);
  gpr_mu_unlock(mu);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(ps);
  track_counters.Finish(state);
}
BENCHMARK(BM_PollAddFd);

class TestClosure : public grpc_closure {
 public:
  virtual ~TestClosure() {}
};

template <class F>
TestClosure* MakeTestClosure(F f) {
  struct C : public TestClosure {
    explicit C(F f) : f_(f) { GRPC_CLOSURE_INIT(this, C::cbfn, this, nullptr); }
    static void cbfn(void* arg, grpc_error_handle /*error*/) {
      C* p = static_cast<C*>(arg);
      p->f_();
    }
    F f_;
  };
  return new C(f);
}

#ifdef GRPC_LINUX_MULTIPOLL_WITH_EPOLL
static void BM_SingleThreadPollOneFd_SpeedOfLight(benchmark::State& state) {
  // equivalent to BM_PollEmptyPollset, but just use the OS primitives to guage
  // what the speed of light would be if we abstracted perfectly
  TrackCounters track_counters;
  int epfd = epoll_create1(0);
  GPR_ASSERT(epfd != -1);
  epoll_event ev[100];
  int fd = eventfd(0, EFD_NONBLOCK);
  ev[0].events = EPOLLIN;
  epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev[0]);
  for (auto _ : state) {
    int err;
    do {
      err = eventfd_write(fd, 1);
    } while (err < 0 && errno == EINTR);
    GPR_ASSERT(err == 0);
    do {
      err = epoll_wait(epfd, ev, GPR_ARRAY_SIZE(ev), 0);
    } while (err < 0 && errno == EINTR);
    GPR_ASSERT(err == 1);
    eventfd_t value;
    do {
      err = eventfd_read(fd, &value);
    } while (err < 0 && errno == EINTR);
    GPR_ASSERT(err == 0);
  }
  close(fd);
  close(epfd);
  track_counters.Finish(state);
}
BENCHMARK(BM_SingleThreadPollOneFd_SpeedOfLight);
#endif

static void BM_SingleThreadPollOneFd(benchmark::State& state) {
  TrackCounters track_counters;
  size_t ps_sz = grpc_pollset_size();
  grpc_pollset* ps = static_cast<grpc_pollset*>(gpr_zalloc(ps_sz));
  gpr_mu* mu;
  grpc_pollset_init(ps, &mu);
  grpc_core::ExecCtx exec_ctx;
  grpc_wakeup_fd wakeup_fd;
  GRPC_ERROR_UNREF(grpc_wakeup_fd_init(&wakeup_fd));
  grpc_fd* wakeup = grpc_fd_create(wakeup_fd.read_fd, "wakeup_read", false);
  grpc_pollset_add_fd(ps, wakeup);
  bool done = false;
  TestClosure* continue_closure = MakeTestClosure([&]() {
    GRPC_ERROR_UNREF(grpc_wakeup_fd_consume_wakeup(&wakeup_fd));
    if (!state.KeepRunning()) {
      done = true;
      return;
    }
    GRPC_ERROR_UNREF(grpc_wakeup_fd_wakeup(&wakeup_fd));
    grpc_fd_notify_on_read(wakeup, continue_closure);
  });
  GRPC_ERROR_UNREF(grpc_wakeup_fd_wakeup(&wakeup_fd));
  grpc_fd_notify_on_read(wakeup, continue_closure);
  gpr_mu_lock(mu);
  while (!done) {
    GRPC_ERROR_UNREF(grpc_pollset_work(ps, nullptr, GRPC_MILLIS_INF_FUTURE));
  }
  grpc_fd_orphan(wakeup, nullptr, nullptr, "done");
  wakeup_fd.read_fd = 0;
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(ps, &shutdown_ps_closure);
  gpr_mu_unlock(mu);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_wakeup_fd_destroy(&wakeup_fd);
  gpr_free(ps);
  track_counters.Finish(state);
  delete continue_closure;
}
BENCHMARK(BM_SingleThreadPollOneFd);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
