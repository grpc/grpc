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

#ifndef GRPC_SUPPORT_LOG_H
#define GRPC_SUPPORT_LOG_H

#include <stdarg.h>
#include <stdlib.h> /* for abort() */

#include <grpc/support/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

GPRAPI void gpr_log_verbosity_init(void);

#define GPR_LOCATION __FILE__, __LINE__

/** Deprecated. **/
GPRAPI int grpc_absl_vlog2_enabled();

/* Deprecated */
GPRAPI void grpc_absl_log_error(const char* file, int line,
                                const char* message_str);

/* Deprecated */
GPRAPI void grpc_absl_log_info(const char* file, int line,
                               const char* message_str);
GPRAPI void grpc_absl_log_info_int(const char* file, int line,
                                   const char* message_str, intptr_t num);

/* Deprecated */
GPRAPI void grpc_absl_vlog(const char* file, int line, const char* message_str);
GPRAPI void grpc_absl_vlog_int(const char* file, int line,
                               const char* message_str, intptr_t num);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_LOG_H */
