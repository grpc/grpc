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

#ifndef GRPC_CORE_LIB_TRANSPORT_STATUS_METADATA_H
#define GRPC_CORE_LIB_TRANSPORT_STATUS_METADATA_H

#include <grpc/support/port_platform.h>

#include <grpc/status.h>

#include "src/core/lib/transport/metadata.h"

grpc_status_code grpc_get_status_code_from_metadata(grpc_mdelem md);

#endif /* GRPC_CORE_LIB_TRANSPORT_STATUS_METADATA_H */
