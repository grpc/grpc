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

#ifndef GRPC_CORE_TSI_ALTS_TRANSPORT_SECURITY_H
#define GRPC_CORE_TSI_ALTS_TRANSPORT_SECURITY_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/thd.h"

typedef struct alts_shared_resource {
  grpc_channel* channel;
  gpr_mu mu;
} alts_shared_resource;

/* This method returns the address of alts_shared_resource object shared by all
 *    TSI handshakes. */
alts_shared_resource* alts_get_shared_resource(void);

#endif /* GRPC_CORE_TSI_ALTS_TRANSPORT_SECURITY_H */
