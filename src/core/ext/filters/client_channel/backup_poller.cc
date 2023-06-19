//
//
// Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/backup_poller.h"

#include <inttypes.h>

#include "absl/status/status.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/timer.h"

#define DEFAULT_POLL_INTERVAL_MS 5000

namespace {
struct backup_poller {
  grpc_timer polling_timer;
  grpc_closure run_poller_closure;
  grpc_closure shutdown_closure;
  gpr_mu* pollset_mu;
  grpc_pollset* pollset;  // guarded by pollset_mu
  bool shutting_down;     // guarded by pollset_mu
  gpr_refcount refs;
  gpr_refcount shutdown_refs;
};
}  // namespace

static gpr_mu g_poller_mu;
static backup_poller* g_poller = nullptr;  // guarded by g_poller_mu
// g_poll_interval_ms is set only once at the first time
// grpc_client_channel_start_backup_polling() is called, after that it is
// treated as const.
static grpc_core::Duration g_poll_interval =
    grpc_core::Duration::Milliseconds(DEFAULT_POLL_INTERVAL_MS);

void grpc_client_channel_global_init_backup_polling() {
  gpr_mu_init(&g_poller_mu);
  int32_t poll_interval_ms =
      grpc_core::ConfigVars::Get().ClientChannelBackupPollIntervalMs();
  if (poll_interval_ms < 0) {
    gpr_log(GPR_ERROR,
            "Invalid GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS: %d, "
            "default value %s will be used.",
            poll_interval_ms, g_poll_interval.ToString().c_str());
  } else {
    g_poll_interval = grpc_core::Duration::Milliseconds(poll_interval_ms);
  }
}

static void backup_poller_shutdown_unref(backup_poller* p) {
  if (gpr_unref(&p->shutdown_refs)) {
    grpc_pollset_destroy(p->pollset);
    gpr_free(p->pollset);
    gpr_free(p);
  }
}

static void done_poller(void* arg, grpc_error_handle /*error*/) {
  backup_poller_shutdown_unref(static_cast<backup_poller*>(arg));
}

static void g_poller_unref() {
  gpr_mu_lock(&g_poller_mu);
  if (gpr_unref(&g_poller->refs)) {
    backup_poller* p = g_poller;
    g_poller = nullptr;
    gpr_mu_unlock(&g_poller_mu);
    gpr_mu_lock(p->pollset_mu);
    p->shutting_down = true;
    grpc_pollset_shutdown(
        p->pollset, GRPC_CLOSURE_INIT(&p->shutdown_closure, done_poller, p,
                                      grpc_schedule_on_exec_ctx));
    gpr_mu_unlock(p->pollset_mu);
    grpc_timer_cancel(&p->polling_timer);
    backup_poller_shutdown_unref(p);
  } else {
    gpr_mu_unlock(&g_poller_mu);
  }
}

static void run_poller(void* arg, grpc_error_handle error) {
  backup_poller* p = static_cast<backup_poller*>(arg);
  if (!error.ok()) {
    if (error != absl::CancelledError()) {
      GRPC_LOG_IF_ERROR("run_poller", error);
    }
    backup_poller_shutdown_unref(p);
    return;
  }
  gpr_mu_lock(p->pollset_mu);
  if (p->shutting_down) {
    gpr_mu_unlock(p->pollset_mu);
    backup_poller_shutdown_unref(p);
    return;
  }
  grpc_error_handle err =
      grpc_pollset_work(p->pollset, nullptr, grpc_core::Timestamp::Now());
  gpr_mu_unlock(p->pollset_mu);
  GRPC_LOG_IF_ERROR("Run client channel backup poller", err);
  grpc_timer_init(&p->polling_timer,
                  grpc_core::Timestamp::Now() + g_poll_interval,
                  &p->run_poller_closure);
}

static void g_poller_init_locked() {
  if (g_poller == nullptr) {
    g_poller = grpc_core::Zalloc<backup_poller>();
    g_poller->pollset =
        static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    g_poller->shutting_down = false;
    grpc_pollset_init(g_poller->pollset, &g_poller->pollset_mu);
    gpr_ref_init(&g_poller->refs, 0);
    // one for timer cancellation, one for pollset shutdown, one for g_poller
    gpr_ref_init(&g_poller->shutdown_refs, 3);
    GRPC_CLOSURE_INIT(&g_poller->run_poller_closure, run_poller, g_poller,
                      grpc_schedule_on_exec_ctx);
    grpc_timer_init(&g_poller->polling_timer,
                    grpc_core::Timestamp::Now() + g_poll_interval,
                    &g_poller->run_poller_closure);
  }
}

void grpc_client_channel_start_backup_polling(
    grpc_pollset_set* interested_parties) {
  if (g_poll_interval == grpc_core::Duration::Zero() ||
      grpc_iomgr_run_in_background()) {
    return;
  }
  gpr_mu_lock(&g_poller_mu);
  g_poller_init_locked();
  gpr_ref(&g_poller->refs);
  // Get a reference to g_poller->pollset before releasing g_poller_mu to make
  // TSAN happy. Otherwise, reading from g_poller (i.e g_poller->pollset) after
  // releasing the lock and setting g_poller to NULL in g_poller_unref() is
  // being flagged as a data-race by TSAN
  grpc_pollset* pollset = g_poller->pollset;
  gpr_mu_unlock(&g_poller_mu);

  grpc_pollset_set_add_pollset(interested_parties, pollset);
}

void grpc_client_channel_stop_backup_polling(
    grpc_pollset_set* interested_parties) {
  if (g_poll_interval == grpc_core::Duration::Zero() ||
      grpc_iomgr_run_in_background()) {
    return;
  }
  grpc_pollset_set_del_pollset(interested_parties, g_poller->pollset);
  g_poller_unref();
}
