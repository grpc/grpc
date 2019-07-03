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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/port.h"

#ifndef GRPC_POSIX_FORK

#include <grpc/fork.h>
#include <grpc/support/log.h>

/*
 * NOTE: FORKING IS NOT GENERALLY SUPPORTED, THIS IS ONLY INTENDED TO WORK
 *       AROUND VERY SPECIFIC USE CASES.
 */

void grpc_prefork() { gpr_log(GPR_ERROR, "Forking not supported on Windows"); }

void grpc_postfork_parent() {}

void grpc_postfork_child() {}

void grpc_fork_handlers_auto_register() {}

#endif  // GRPC_POSIX_FORK
