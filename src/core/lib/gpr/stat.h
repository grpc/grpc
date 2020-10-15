/*
 *
 * Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_GPR_STAT_H
#define GRPC_CORE_LIB_GPR_STAT_H

#include <grpc/support/port_platform.h>

#include <stdio.h>
#include <time.h>

/* Gets the last-modified timestamp of a file or a directory. If error occurs,
 * the epoch time will be returned. */
time_t gpr_last_modified_timestamp(const char* filename);

#endif /* GRPC_CORE_LIB_GPR_STAT_H */
