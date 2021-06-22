// Copyright 2021 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#ifdef GRPC_USE_EVENT_ENGINE
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine/closure.h"
#include "src/core/lib/iomgr/event_engine/iomgr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/init.h"
#include "src/core/lib/transport/error_utils.h"

namespace {
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GrpcClosureToCallback;

void timer_init(grpc_timer* timer, grpc_millis deadline,
                grpc_closure* closure) {
  timer->ee_task_handle = grpc_iomgr_event_engine()->RunAt(
      grpc_core::ToAbslTime(
          grpc_millis_to_timespec(deadline, GPR_CLOCK_REALTIME)),
      GrpcClosureToCallback(closure, GRPC_ERROR_NONE), {});
}

void timer_cancel(grpc_timer* timer) {
  auto handle = timer->ee_task_handle;
  grpc_iomgr_event_engine()->TryCancel(handle);
}

/* Internal API */
grpc_timer_check_result timer_check(grpc_millis* /* next */) {
  return GRPC_TIMERS_NOT_CHECKED;
}
void timer_list_init() {}
void timer_list_shutdown(void) {}
void timer_consume_kick(void) {}

}  // namespace

grpc_timer_vtable grpc_event_engine_timer_vtable = {
    timer_init,      timer_cancel,        timer_check,
    timer_list_init, timer_list_shutdown, timer_consume_kick};

#endif  // GRPC_USE_EVENT_ENGINE
