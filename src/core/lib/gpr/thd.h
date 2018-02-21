/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPR_THD_H
#define GRPC_CORE_LIB_GPR_THD_H
/** Internal thread interface for GPR.

   Types
        gpr_thd_options   options used when creating a thread
 */

#include <grpc/support/port_platform.h>
#include <grpc/support/thd_id.h>
#include <grpc/support/time.h>

/** Thread creation options. */
typedef struct {
  int flags; /** Opaque field. Get and set with accessors below. */
} gpr_thd_options;

/** Create a new thread running (*thd_body)(arg) and place its thread identifier
   in *t, and return true.  If there are insufficient resources, return false.
   thd_name is the name of the thread for identification purposes on platforms
   that support thread naming.
   If options==NULL, default options are used.
   The thread is immediately runnable, and exits when (*thd_body)() returns.  */
int gpr_thd_new(gpr_thd_id* t, const char* thd_name,
                void (*thd_body)(void* arg), void* arg,
                const gpr_thd_options* options);

/** Return a gpr_thd_options struct with all fields set to defaults. */
gpr_thd_options gpr_thd_options_default(void);

/** Set the thread to become detached on startup - this is the default. */
void gpr_thd_options_set_detached(gpr_thd_options* options);

/** Set the thread to become joinable - mutually exclusive with detached. */
void gpr_thd_options_set_joinable(gpr_thd_options* options);

/** Returns non-zero if the option detached is set. */
int gpr_thd_options_is_detached(const gpr_thd_options* options);

/** Returns non-zero if the option joinable is set. */
int gpr_thd_options_is_joinable(const gpr_thd_options* options);

/** Blocks until the specified thread properly terminates.
   Calling this on a detached thread has unpredictable results. */
void gpr_thd_join(gpr_thd_id t);

/* Internal interfaces between modules within the gpr support library.  */
void gpr_thd_init();

/* Wait for all outstanding threads to finish, up to deadline */
int gpr_await_threads(gpr_timespec deadline);

#endif /* GRPC_CORE_LIB_GPR_THD_H */
