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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <thread>
#include <vector>

#include "test/cpp/util/subprocess.h"
#include "test/cpp/util/test_config.h"

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/resolver.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

// TODO: pull in different headers when enabling this
// test on windows. Also set BAD_SOCKET_RETURN_VAL
// to INVALID_SOCKET on windows.
#include "src/core/lib/iomgr/sockaddr_posix.h"
#define BAD_SOCKET_RETURN_VAL -1

using grpc::SubProcess;
using std::vector;
using testing::UnorderedElementsAreArray;

// Hack copied from "test/cpp/end2end/server_crash_test_client.cc"!
// In some distros, gflags is in the namespace google, and in some others,
// in gflags. This hack is enabling us to find both.
namespace google {}
namespace gflags {}
using namespace google;
using namespace gflags;

DEFINE_string(target_name, "", "Target name to resolve.");
DEFINE_string(
    local_dns_server_address, "",
    "Optional. This address is placed as the uri authority if present.");

namespace {

gpr_timespec TestDeadline(void) {
  return grpc_timeout_seconds_to_deadline(100);
}

struct ArgsStruct {
  gpr_event ev;
  gpr_atm done_atm;
  gpr_mu* mu;
  grpc_pollset* pollset;
  grpc_pollset_set* pollset_set;
  grpc_combiner* lock;
  grpc_channel_args* channel_args;
};

void ArgsInit(ArgsStruct* args) {
  gpr_event_init(&args->ev);
  args->pollset = (grpc_pollset*)gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(args->pollset, &args->mu);
  args->pollset_set = grpc_pollset_set_create();
  grpc_pollset_set_add_pollset(args->pollset_set, args->pollset);
  args->lock = grpc_combiner_create();
  gpr_atm_rel_store(&args->done_atm, 0);
  args->channel_args = nullptr;
}

void DoNothing(void* arg, grpc_error* error) {}

void ArgsFinish(ArgsStruct* args) {
  GPR_ASSERT(gpr_event_wait(&args->ev, TestDeadline()));
  grpc_pollset_set_del_pollset(args->pollset_set, args->pollset);
  grpc_pollset_set_destroy(args->pollset_set);
  grpc_closure DoNothing_cb;
  GRPC_CLOSURE_INIT(&DoNothing_cb, DoNothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(args->pollset, &DoNothing_cb);
  // exec_ctx needs to be flushed before calling grpc_pollset_destroy()
  grpc_channel_args_destroy(args->channel_args);
  grpc_core::ExecCtx::Get()->Flush();
  grpc_pollset_destroy(args->pollset);
  gpr_free(args->pollset);
  GRPC_COMBINER_UNREF(args->lock, nullptr);
}

gpr_timespec NSecondDeadline(int seconds) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_seconds(seconds, GPR_TIMESPAN));
}

void PollPollsetUntilRequestDone(ArgsStruct* args) {
  gpr_timespec deadline = NSecondDeadline(10);
  while (true) {
    bool done = gpr_atm_acq_load(&args->done_atm) != 0;
    if (done) {
      break;
    }
    gpr_timespec time_left =
        gpr_time_sub(deadline, gpr_now(GPR_CLOCK_REALTIME));
    gpr_log(GPR_DEBUG, "done=%d, time_left=%" PRId64 ".%09d", done,
            time_left.tv_sec, time_left.tv_nsec);
    GPR_ASSERT(gpr_time_cmp(time_left, gpr_time_0(GPR_TIMESPAN)) >= 0);
    grpc_pollset_worker* worker = nullptr;
    grpc_core::ExecCtx exec_ctx;
    gpr_mu_lock(args->mu);
    GRPC_LOG_IF_ERROR("pollset_work",
                      grpc_pollset_work(args->pollset, &worker,
                                        grpc_timespec_to_millis_round_up(
                                            NSecondDeadline(1))));
    gpr_mu_unlock(args->mu);
  }
  gpr_event_set(&args->ev, (void*)1);
}

void OpenAndCloseSocketsStressLoop(int dummy_port, gpr_event* done_ev) {
  // The goal of this loop is to catch socket
  // "use after close" bugs within the c-ares resolver by acting
  // like some separate thread doing I/O.
  // It's goal is to try to hit race conditions whereby:
  //    1) The c-ares resolver closes a socket.
  //    2) This loop opens a socket with (coincidentally) the same handle.
  //    3) the c-ares resolver mistakenly uses that same socket without
  //       realizing that its closed.
  //    4) This loop performs an operation on that socket that should
  //       succeed but instead fails because of what the c-ares
  //       resolver did in the meantime.
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(dummy_port);
  ((char*)&addr.sin6_addr)[15] = 1;
  for (;;) {
    if (gpr_event_get(done_ev)) {
      return;
    }
    std::vector<int> sockets;
    // First open a bunch of sockets, bind and listen
    // '50' is an arbitrary number that, experimentally,
    // has a good chance of catching bugs.
    for (size_t i = 0; i < 50; i++) {
      int s = socket(AF_INET6, SOCK_STREAM, 0);
      int val = 1;
      setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
      fcntl(s, F_SETFL, O_NONBLOCK);
      ASSERT_TRUE(s != BAD_SOCKET_RETURN_VAL)
          << "Failed to create TCP ipv6 socket";
      gpr_log(GPR_DEBUG, "Opened fd: %d", s);
      ASSERT_TRUE(bind(s, (const sockaddr*)&addr, sizeof(addr)) == 0)
          << "Failed to bind socket " + std::to_string(s) +
                 " to [::1]:" + std::to_string(dummy_port) +
                 ". errno: " + std::to_string(errno);
      ASSERT_TRUE(listen(s, 1) == 0) << "Failed to listen on socket " +
                                            std::to_string(s) +
                                            ". errno: " + std::to_string(errno);
      sockets.push_back(s);
    }
    // Do a non-blocking accept followed by a close on all of those sockets.
    // Do this in a separate loop to try to induce a time window to hit races.
    for (size_t i = 0; i < sockets.size(); i++) {
      gpr_log(GPR_DEBUG, "non-blocking accept then close on %d", sockets[i]);
      if (accept(sockets[i], nullptr, nullptr)) {
        // If e.g. a "shutdown" was called on this fd from another thread,
        // then this accept call should fail with an unexpected error.
        ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK)
            << "OpenAndCloseSocketsStressLoop accept on socket " +
                   std::to_string(sockets[i]) +
                   " failed in "
                   "an unexpected way. "
                   "errno: " +
                   std::to_string(errno) +
                   ". Socket use-after-close bugs are likely.";
      }
      ASSERT_TRUE(close(sockets[i]) == 0)
          << "Failed to close socket: " + std::to_string(sockets[i]) +
                 ". errno: " + std::to_string(errno);
    }
  }
}

void CheckResolvedWithoutErrorLocked(void* argsp, grpc_error* err) {
  EXPECT_EQ(err, GRPC_ERROR_NONE);
  ArgsStruct* args = (ArgsStruct*)argsp;
  gpr_atm_rel_store(&args->done_atm, 1);
  gpr_mu_lock(args->mu);
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, nullptr));
  gpr_mu_unlock(args->mu);
}

void RunResolvesRecordsTest() {
  grpc_core::ExecCtx exec_ctx;
  ArgsStruct args;
  ArgsInit(&args);
  // maybe build the address with an authority
  char* whole_uri = nullptr;
  GPR_ASSERT(asprintf(&whole_uri, "dns://%s/%s",
                      FLAGS_local_dns_server_address.c_str(),
                      FLAGS_target_name.c_str()));
  // create resolver and resolve
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      grpc_core::ResolverRegistry::CreateResolver(whole_uri, nullptr,
                                                  args.pollset_set, args.lock);
  gpr_free(whole_uri);
  grpc_closure on_resolver_result_changed;
  GRPC_CLOSURE_INIT(&on_resolver_result_changed,
                    CheckResolvedWithoutErrorLocked, (void*)&args,
                    grpc_combiner_scheduler(args.lock));
  resolver->NextLocked(&args.channel_args, &on_resolver_result_changed);
  grpc_core::ExecCtx::Get()->Flush();
  PollPollsetUntilRequestDone(&args);
  ArgsFinish(&args);
}

TEST(ResolverComponentTest, TestResolvesRelevantRecordsWithConcurrentFdStress) {
  // Start up background stress thread
  int dummy_port = grpc_pick_unused_port_or_die();
  gpr_event done_ev;
  gpr_event_init(&done_ev);
  std::thread socket_stress_thread(OpenAndCloseSocketsStressLoop, dummy_port,
                                   &done_ev);
  // Run the resolver test
  RunResolvesRecordsTest();
  // Shutdown and join stress thread
  gpr_event_set(&done_ev, (void*)1);
  socket_stress_thread.join();
}

}  // namespace

int main(int argc, char** argv) {
  grpc_init();
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_target_name == "") {
    gpr_log(GPR_ERROR, "Missing target_name param.");
    abort();
  }
  if (FLAGS_local_dns_server_address != "") {
    gpr_log(GPR_INFO, "Specifying authority in uris to: %s",
            FLAGS_local_dns_server_address.c_str());
  }
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
