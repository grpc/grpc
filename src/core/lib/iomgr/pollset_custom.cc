/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#include <stddef.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_custom.h"
#include "src/core/lib/iomgr/timer.h"

#include "src/core/lib/debug/trace.h"

static grpc_custom_poller_vtable* poller_vtable;

struct grpc_pollset {
  gpr_mu mu;
};

static size_t pollset_size() { return sizeof(grpc_pollset); }

static void pollset_global_init() { poller_vtable->init(); }

static void pollset_global_shutdown() { poller_vtable->shutdown(); }

static void pollset_init(grpc_pollset* pollset, gpr_mu** mu) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  gpr_mu_init(&pollset->mu);
  *mu = &pollset->mu;
}

static void pollset_shutdown(grpc_pollset* /*pollset*/, grpc_closure* closure) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
}

static void pollset_destroy(grpc_pollset* pollset) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  gpr_mu_destroy(&pollset->mu);
}

static grpc_error_handle pollset_work(grpc_pollset* pollset,
                                      grpc_pollset_worker** /*worker_hdl*/,
                                      grpc_millis deadline) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  gpr_mu_unlock(&pollset->mu);
  grpc_millis now = grpc_core::ExecCtx::Get()->Now();
  grpc_millis timeout = 0;
  if (deadline > now) {
    timeout = deadline - now;
  }
  // We yield here because the poll() call might yield
  // control back to the application
  grpc_core::ExecCtx* curr = grpc_core::ExecCtx::Get();
  grpc_core::ExecCtx::Set(nullptr);
  poller_vtable->poll(static_cast<size_t>(timeout));
  grpc_core::ExecCtx::Set(curr);
  grpc_core::ExecCtx::Get()->InvalidateNow();
  if (grpc_core::ExecCtx::Get()->HasWork()) {
    grpc_core::ExecCtx::Get()->Flush();
  }
  gpr_mu_lock(&pollset->mu);
  return GRPC_ERROR_NONE;
}

static grpc_error_handle pollset_kick(
    grpc_pollset* /*pollset*/, grpc_pollset_worker* /*specific_worker*/) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  poller_vtable->kick();
  return GRPC_ERROR_NONE;
}

grpc_pollset_vtable custom_pollset_vtable = {
    pollset_global_init, pollset_global_shutdown,
    pollset_init,        pollset_shutdown,
    pollset_destroy,     pollset_work,
    pollset_kick,        pollset_size};

void grpc_custom_pollset_init(grpc_custom_poller_vtable* vtable) {
  poller_vtable = vtable;
  grpc_set_pollset_vtable(&custom_pollset_vtable);
}
