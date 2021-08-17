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

#include "src/core/lib/iomgr/timer.h"

#include "src/core/lib/iomgr/timer_manager.h"

grpc_timer_vtable* grpc_timer_impl;

void grpc_set_timer_impl(grpc_timer_vtable* vtable) {
  grpc_timer_impl = vtable;
}

void grpc_timer_init(grpc_timer* timer, grpc_millis deadline,
                     grpc_closure* closure) {
  grpc_timer_impl->init(timer, deadline, closure);
}

void grpc_timer_cancel(grpc_timer* timer) { grpc_timer_impl->cancel(timer); }

grpc_timer_check_result grpc_timer_check(grpc_millis* next) {
  return grpc_timer_impl->check(next);
}

void grpc_timer_list_init() { grpc_timer_impl->list_init(); }

void grpc_timer_list_shutdown() { grpc_timer_impl->list_shutdown(); }

void grpc_timer_consume_kick() { grpc_timer_impl->consume_kick(); }
