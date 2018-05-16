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

#ifndef GRPC_IMPL_CODEGEN_SYNC_POSIX_H
#define GRPC_IMPL_CODEGEN_SYNC_POSIX_H

#include <grpc/impl/codegen/port_platform.h>

#include <grpc/impl/codegen/sync_generic.h>

#include <pthread.h>

typedef pthread_mutex_t gpr_mu;
typedef pthread_cond_t gpr_cv;
typedef pthread_once_t gpr_once;

#define GPR_ONCE_INIT PTHREAD_ONCE_INIT

#endif /* GRPC_IMPL_CODEGEN_SYNC_POSIX_H */
