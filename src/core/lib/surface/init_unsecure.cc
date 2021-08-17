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

#include <grpc/support/port_platform.h>

#include <grpc/support/log.h>

#include "src/core/lib/surface/init.h"

void grpc_security_pre_init(void) {}

void grpc_register_security_filters(void) {}

void grpc_security_init(void) {
  gpr_log(GPR_DEBUG,
          "Using insecure gRPC build. Security handshakers will not be invoked "
          "even if secure credentials are used.");
}
