/*
 *
 * Copyright 2015-2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SURFACE_ALARM_INTERNAL_H
#define GRPC_CORE_LIB_SURFACE_ALARM_INTERNAL_H

#include <grpc/support/log.h>
#include "src/core/lib/debug/trace.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_alarm_refcount;

#ifndef NDEBUG

#define GRPC_ALARM_REF(a, reason) alarm_ref_dbg(a, reason, __FILE__, __LINE__)
#define GRPC_ALARM_UNREF(a, reason) \
  alarm_unref_dbg(a, reason, __FILE__, __LINE__)

#else /* !defined(NDEBUG) */

#define GRPC_ALARM_REF(a, reason) alarm_ref(a)
#define GRPC_ALARM_UNREF(a, reason) alarm_unref(a)

#endif /* defined(NDEBUG) */

#endif /* GRPC_CORE_LIB_SURFACE_ALARM_INTERNAL_H */
