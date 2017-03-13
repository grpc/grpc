/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Test out pollset latencies */

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

extern "C" {
#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/wakeup_fd_posix.h"
}

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
  grpc_pollset_destroy(static_cast<grpc_pollset*>(ps));
}

static void BM_CreateDestroyPollset(benchmark::State& state) {
  TrackCounters track_counters;
  size_t ps_sz = grpc_pollset_size();
  grpc_pollset* ps = static_cast<grpc_pollset*>(gpr_malloc(ps_sz));
  gpr_mu* mu;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure shutdown_ps_closure;
  grpc_closure_init(&shutdown_ps_closure, shutdown_ps, ps,
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
  gpr_timespec now = gpr_time_0(GPR_CLOCK_MONOTONIC);
  gpr_timespec deadline = gpr_inf_past(GPR_CLOCK_MONOTONIC);
  gpr_mu_lock(mu);
  while (state.KeepRunning()) {
    grpc_pollset_worker* worker;
    GRPC_ERROR_UNREF(grpc_pollset_work(&exec_ctx, ps, &worker, now, deadline));
  }
  grpc_closure shutdown_ps_closure;
  grpc_closure_init(&shutdown_ps_closure, shutdown_ps, ps,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(&exec_ctx, ps, &shutdown_ps_closure);
  gpr_mu_unlock(mu);
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(ps);
  track_counters.Finish(state);
}
BENCHMARK(BM_PollEmptyPollset);

class Closure : public grpc_closure {
 public:
  virtual ~Closure() {}
};

template <class F>
Closure* MakeClosure(F f, grpc_closure_scheduler* scheduler) {
  struct C : public Closure {
    C(F f, grpc_closure_scheduler* scheduler) : f_(f) {
      grpc_closure_init(this, C::cbfn, this, scheduler);
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
  gpr_timespec now = gpr_time_0(GPR_CLOCK_MONOTONIC);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
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
    grpc_pollset_worker* worker;
    GRPC_ERROR_UNREF(grpc_pollset_work(&exec_ctx, ps, &worker, now, deadline));
  }
  grpc_fd_orphan(&exec_ctx, wakeup, NULL, NULL, "done");
  wakeup_fd.read_fd = 0;
  grpc_closure shutdown_ps_closure;
  grpc_closure_init(&shutdown_ps_closure, shutdown_ps, ps,
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
