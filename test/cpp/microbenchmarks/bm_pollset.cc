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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"

#include "test/cpp/microbenchmarks/helpers.h"
#include "third_party/benchmark/include/benchmark/benchmark.h"

#include <string.h>

#ifdef GRPC_LINUX_MULTIPOLL_WITH_EPOLL
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

auto& force_library_initialization = Library::get();

static void shutdown_ps(grpc_exec_ctx* exec_ctx, void* ps, grpc_error* error) {
  grpc_pollset_destroy(exec_ctx, static_cast<grpc_pollset*>(ps));
}

static void BM_CreateDestroyPollset(benchmark::State& state) {
  TrackCounters track_counters;
  size_t ps_sz = grpc_pollset_size();
  grpc_pollset* ps = static_cast<grpc_pollset*>(gpr_malloc(ps_sz));
  gpr_mu* mu;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  while (state.KeepRunning()) {
    memset(ps, 0, ps_sz);
    grpc_pollset_init(ps, &mu);
    gpr_mu_lock(mu);
    grpc_pollset_shutdown(&exec_ctx, ps, &shutdown_ps_closure);
    gpr_mu_unlock(mu);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_exec_ctx_finish(&exec_ctx);
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
  while (state.KeepRunning()) {
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
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_mu_lock(mu);
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(grpc_pollset_work(&exec_ctx, ps, nullptr, 0));
  }
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, ps, &shutdown_ps_closure);
  gpr_mu_unlock(mu);
  grpc_exec_ctx_finish(&exec_ctx);
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
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_wakeup_fd wakeup_fd;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("wakeup_fd_init", grpc_wakeup_fd_init(&wakeup_fd)));
  grpc_fd* fd = grpc_fd_create(wakeup_fd.read_fd, "xxx");
  while (state.KeepRunning()) {
    grpc_pollset_add_fd(&exec_ctx, ps, fd);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  grpc_fd_orphan(&exec_ctx, fd, nullptr, nullptr, false /* already_closed */,
                 "xxx");
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  gpr_mu_lock(mu);
  grpc_pollset_shutdown(&exec_ctx, ps, &shutdown_ps_closure);
  gpr_mu_unlock(mu);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(ps);
  track_counters.Finish(state);
}
BENCHMARK(BM_PollAddFd);

class Closure : public grpc_closure {
 public:
  virtual ~Closure() {}
};

template <class F>
Closure* MakeClosure(F f, grpc_closure_scheduler* scheduler) {
  struct C : public Closure {
    C(F f, grpc_closure_scheduler* scheduler) : f_(f) {
      GRPC_CLOSURE_INIT(this, C::cbfn, this, scheduler);
    }
    static void cbfn(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {
      C* p = static_cast<C*>(arg);
      p->f_();
    }
    F f_;
  };
  return new C(f, scheduler);
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
  while (state.KeepRunning()) {
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
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_wakeup_fd wakeup_fd;
  GRPC_ERROR_UNREF(grpc_wakeup_fd_init(&wakeup_fd));
  grpc_fd* wakeup = grpc_fd_create(wakeup_fd.read_fd, "wakeup_read");
  grpc_pollset_add_fd(&exec_ctx, ps, wakeup);
  bool done = false;
  Closure* continue_closure = MakeClosure(
      [&]() {
        GRPC_ERROR_UNREF(grpc_wakeup_fd_consume_wakeup(&wakeup_fd));
        if (!state.KeepRunning()) {
          done = true;
          return;
        }
        GRPC_ERROR_UNREF(grpc_wakeup_fd_wakeup(&wakeup_fd));
        grpc_fd_notify_on_read(&exec_ctx, wakeup, continue_closure);
      },
      grpc_schedule_on_exec_ctx);
  GRPC_ERROR_UNREF(grpc_wakeup_fd_wakeup(&wakeup_fd));
  grpc_fd_notify_on_read(&exec_ctx, wakeup, continue_closure);
  gpr_mu_lock(mu);
  while (!done) {
    GRPC_ERROR_UNREF(
        grpc_pollset_work(&exec_ctx, ps, nullptr, GRPC_MILLIS_INF_FUTURE));
  }
  grpc_fd_orphan(&exec_ctx, wakeup, nullptr, nullptr,
                 false /* already_closed */, "done");
  wakeup_fd.read_fd = 0;
  grpc_closure shutdown_ps_closure;
  GRPC_CLOSURE_INIT(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, ps, &shutdown_ps_closure);
  gpr_mu_unlock(mu);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_wakeup_fd_destroy(&wakeup_fd);
  gpr_free(ps);
  track_counters.Finish(state);
  delete continue_closure;
}
BENCHMARK(BM_SingleThreadPollOneFd);

BENCHMARK_MAIN();
